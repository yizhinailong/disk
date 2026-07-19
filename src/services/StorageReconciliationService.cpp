/**
 * @file StorageReconciliationService.cpp
 * @brief 存储、引用和配额一致性巡检服务实现
 */

#include "services/StorageReconciliationService.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "services/StorageJobRepository.hpp"
#include "storage/IBlobStore.hpp"
#include "storage/UploadStagingStorage.hpp"
#include "utils/FileHashUtil.hpp"
#include "utils/LogHelper.hpp"

namespace disk::reconciliation {
    namespace {
        constexpr std::string_view kContentRefCountMismatch = "content_ref_count_mismatch";
        constexpr std::string_view kZeroReferenceContent = "zero_reference_content";
        constexpr std::string_view kMissingFinalBlob = "missing_final_blob";
        constexpr std::string_view kFinalBlobSizeMismatch = "final_blob_size_mismatch";
        constexpr std::string_view kQuotaUsedMismatch = "quota_used_mismatch";
        constexpr std::string_view kQuotaReservedMismatch = "quota_reserved_mismatch";
        constexpr std::string_view kOrphanStagingObject = "orphan_staging_object";
        constexpr std::string_view kOrphanFinalBlob = "orphan_final_blob";

        [[nodiscard]] auto SerializeJson(const Json::Value& value) -> std::string {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            return Json::writeString(builder, value);
        }

        [[nodiscard]] auto IsSafeScanId(std::string_view scan_id) -> bool {
            return !scan_id.empty() && scan_id.size() <= 64 &&
                   std::ranges::all_of(scan_id, [](unsigned char character) {
                       return std::isalnum(character) != 0 || character == '.' ||
                              character == '_' || character == ':' || character == '-';
                   });
        }

        [[nodiscard]] auto MakeDetails(const std::string& scan_id) -> Json::Value {
            Json::Value details(Json::objectValue);
            details["scan_id"] = scan_id;
            return details;
        }

        [[nodiscard]] auto MakeBlobGcJob(
            uint64_t content_id,
            const std::string& storage_path
        ) -> disk::jobs::NewStorageJob {
            Json::Value payload(Json::objectValue);
            payload["content_id"] = Json::UInt64(content_id);
            payload["storage_path"] = storage_path;
            const auto aggregate_id = std::to_string(content_id);
            return disk::jobs::NewStorageJob{
                .job_type = std::string(disk::jobs::kBlobGcJobType),
                .aggregate_id = aggregate_id,
                .dedupe_key = "blob-gc:" + aggregate_id,
                .payload = std::move(payload),
            };
        }

        [[nodiscard]] auto AddWithoutOverflow(uint64_t left, uint64_t right) -> uint64_t {
            if (right > std::numeric_limits<uint64_t>::max() - left) {
                throw std::overflow_error("Reconciliation byte total overflow");
            }
            return left + right;
        }
    } // namespace

    auto ValidateReconciliationPageRequest(const ReconciliationPageRequest& request)
        -> Result<void> {
        if (!IsSafeScanId(request.scan_id)) {
            return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Invalid reconciliation scan ID")
            );
        }

        const auto is_database_scope =
            request.scope == ReconciliationScope::Contents ||
            request.scope == ReconciliationScope::Users;
        const auto maximum = is_database_scope ? kMaxDatabaseReconciliationPageSize : kMaxObjectReconciliationPageSize;
        if (request.limit == 0 || request.limit > maximum) {
            return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Invalid reconciliation page size")
            );
        }
        if (is_database_scope && !request.continuation_token.empty()) {
            return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Database reconciliation cannot use an object cursor")
            );
        }
        if (!is_database_scope && request.after_id != 0) {
            return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Object reconciliation cannot use a database cursor")
            );
        }
        if (request.continuation_token.size() > 4096) {
            return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Reconciliation continuation token is too long")
            );
        }
        return {};
    }

    auto BuildObjectResourceId(std::string_view locator) -> std::string {
        return disk::utils::FileHashUtil::HashSha256(std::string(locator));
    }

    StorageReconciliationService::StorageReconciliationService(
        drogon::orm::DbClientPtr db_client,
        disk::storage::UploadStagingStorage* staging_storage,
        disk::storage::IBlobStore* blob_store
    ) : m_db_client(std::move(db_client)),
        m_staging_storage(staging_storage),
        m_blob_store(blob_store) {}

    auto StorageReconciliationService::RunPage(const ReconciliationPageRequest& request) const
        -> drogon::Task<Result<ReconciliationPageResult>> {
        auto validation = ValidateReconciliationPageRequest(request);
        if (!validation) {
            co_return std::unexpected(validation.error());
        }
        if (m_db_client == nullptr) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Reconciliation database is not configured")
            );
        }

        try {
            switch (request.scope) {
                case ReconciliationScope::Contents:
                    co_return co_await RunContentPage(request);
                case ReconciliationScope::Users:
                    co_return co_await RunUserPage(request);
                case ReconciliationScope::Staging:
                case ReconciliationScope::Final:
                    co_return co_await RunObjectPage(request);
            }
        } catch (const drogon::orm::DrogonDbException& error) {
            Logger::Warn() << "Storage reconciliation database failure: scope="
                           << ToStorageValue(request.scope) << ", scan_id=" << request.scan_id
                           << ", error=" << error.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Storage reconciliation database failure")
            );
        } catch (const std::exception& error) {
            Logger::Warn() << "Storage reconciliation failure: scope="
                           << ToStorageValue(request.scope) << ", scan_id=" << request.scan_id
                           << ", error=" << error.what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Storage reconciliation failed")
            );
        }

        co_return std::unexpected(
            ErrorInfo(ErrorCode::ValidationFailed, "Unsupported reconciliation scope")
        );
    }

    auto StorageReconciliationService::RecordFinding(
        const ReconciliationFinding& finding
    ) const -> drogon::Task<void> {
        if (finding.finding_type.empty() || finding.finding_type.size() > 64 ||
            finding.resource_id.empty() || finding.resource_id.size() > 128 ||
            (finding.resource_locator.has_value() &&
             finding.resource_locator->size() > 1024)) {
            throw std::invalid_argument("Invalid reconciliation finding identity");
        }

        co_await m_db_client->execSqlCoro(
            "INSERT INTO storage_reconciliation_findings " "  (finding_type, resource_id, resource_locator, severity, " "   resolution_strategy, details) " "VALUES ($1, $2, NULLIF($3, ''), $4, $5, $6::jsonb) " "ON CONFLICT (finding_type, resource_id) DO UPDATE SET " "  resource_locator = EXCLUDED.resource_locator, " "  severity = EXCLUDED.severity, " "  resolution_strategy = EXCLUDED.resolution_strategy, " "  details = EXCLUDED.details, " "  occurrences = storage_reconciliation_findings.occurrences + 1, " "  last_seen_at = NOW(), resolved_at = NULL",
            finding.finding_type,
            finding.resource_id,
            finding.resource_locator.value_or(std::string{}),
            static_cast<int16_t>(finding.severity),
            std::string(ToStorageValue(finding.resolution_strategy)),
            SerializeJson(finding.details)
        );
    }

    auto StorageReconciliationService::ResolveFinding(
        std::string_view finding_type,
        std::string_view resource_id
    ) const -> drogon::Task<void> {
        co_await m_db_client->execSqlCoro(
            "UPDATE storage_reconciliation_findings SET " "  resolved_at = COALESCE(resolved_at, NOW()), last_seen_at = NOW() " "WHERE finding_type = $1 AND resource_id = $2 AND resolved_at IS NULL",
            std::string(finding_type),
            std::string(resource_id)
        );
    }

    auto StorageReconciliationService::RunContentPage(
        const ReconciliationPageRequest& request
    ) const -> drogon::Task<Result<ReconciliationPageResult>> {
        auto rows = co_await m_db_client->execSqlCoro(
            "SELECT content.id, content.hash_md5, content.size, content.storage_path, " "       content.ref_count, " "       ((SELECT COUNT(*) FROM files WHERE content_id = content.id) + " "        (SELECT COUNT(*) FROM trash " "         WHERE content_id = content.id AND item_type = 'file')) AS actual_ref_count " "FROM file_contents AS content " "WHERE content.id > $1 ORDER BY content.id LIMIT $2",
            static_cast<int64_t>(request.after_id),
            static_cast<int64_t>(request.limit)
        );

        ReconciliationPageResult page;
        page.inspected = rows.size();
        disk::jobs::StorageJobRepository job_repository(m_db_client);
        for (const auto& row : rows) {
            const auto content_id = row["id"].as<uint64_t>();
            const auto resource_id = std::to_string(content_id);
            const auto storage_path = row["storage_path"].as<std::string>();
            const auto stored_ref_count = row["ref_count"].as<int64_t>();
            const auto actual_ref_count = row["actual_ref_count"].as<uint64_t>();
            const auto expected_size = row["size"].as<uint64_t>();
            page.next_after_id = content_id;

            if (stored_ref_count != static_cast<int64_t>(actual_ref_count)) {
                auto details = MakeDetails(request.scan_id);
                details["stored_ref_count"] = Json::Int64(stored_ref_count);
                details["actual_ref_count"] = Json::UInt64(actual_ref_count);
                co_await RecordFinding(ReconciliationFinding{
                    .finding_type = std::string(kContentRefCountMismatch),
                    .resource_id = resource_id,
                    .resource_locator = "file_contents/" + resource_id,
                    .severity = ReconciliationSeverity::Critical,
                    .resolution_strategy = ResolutionStrategy::Alert,
                    .details = std::move(details),
                });
                ++page.findings_recorded;
            } else {
                co_await ResolveFinding(kContentRefCountMismatch, resource_id);
            }

            if (ShouldEnqueueBlobGc(stored_ref_count, actual_ref_count)) {
                auto details = MakeDetails(request.scan_id);
                details["storage_path"] = storage_path;
                co_await RecordFinding(ReconciliationFinding{
                    .finding_type = std::string(kZeroReferenceContent),
                    .resource_id = resource_id,
                    .resource_locator = "file_contents/" + resource_id,
                    .severity = ReconciliationSeverity::Warning,
                    .resolution_strategy = ResolutionStrategy::AutoGc,
                    .details = std::move(details),
                });
                ++page.findings_recorded;
                if (co_await job_repository.EnqueueOrRearmSucceeded(
                        m_db_client,
                        MakeBlobGcJob(content_id, storage_path)
                    )) {
                    ++page.repairs_enqueued;
                }
                continue;
            }
            co_await ResolveFinding(kZeroReferenceContent, resource_id);

            if (actual_ref_count == 0) {
                continue;
            }
            if (m_blob_store == nullptr) {
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Reconciliation Blob store is not configured")
                );
            }

            const disk::storage::BlobDescriptor blob{
                .content_id = content_id,
                .hash_md5 = row["hash_md5"].as<std::string>(),
                .storage_path = storage_path,
                .size = expected_size,
            };
            auto exists = co_await m_blob_store->BlobExists(blob);
            if (!exists) {
                co_return std::unexpected(exists.error());
            }
            if (!exists.value()) {
                auto details = MakeDetails(request.scan_id);
                details["expected_size"] = Json::UInt64(expected_size);
                co_await RecordFinding(ReconciliationFinding{
                    .finding_type = std::string(kMissingFinalBlob),
                    .resource_id = resource_id,
                    .resource_locator = storage_path,
                    .severity = ReconciliationSeverity::Critical,
                    .resolution_strategy = ResolutionStrategy::Manual,
                    .details = std::move(details),
                });
                ++page.findings_recorded;
                co_await ResolveFinding(kFinalBlobSizeMismatch, resource_id);
                continue;
            }
            co_await ResolveFinding(kMissingFinalBlob, resource_id);

            auto observed_size = co_await m_blob_store->GetFileSize(storage_path);
            if (!observed_size) {
                if (observed_size.error().code == ErrorCode::FileNotFound) {
                    auto details = MakeDetails(request.scan_id);
                    details["expected_size"] = Json::UInt64(expected_size);
                    co_await RecordFinding(ReconciliationFinding{
                        .finding_type = std::string(kMissingFinalBlob),
                        .resource_id = resource_id,
                        .resource_locator = storage_path,
                        .severity = ReconciliationSeverity::Critical,
                        .resolution_strategy = ResolutionStrategy::Manual,
                        .details = std::move(details),
                    });
                    ++page.findings_recorded;
                    co_await ResolveFinding(kFinalBlobSizeMismatch, resource_id);
                    continue;
                }
                co_return std::unexpected(observed_size.error());
            }
            if (observed_size.value() != expected_size) {
                auto details = MakeDetails(request.scan_id);
                details["expected_size"] = Json::UInt64(expected_size);
                details["observed_size"] = Json::UInt64(observed_size.value());
                co_await RecordFinding(ReconciliationFinding{
                    .finding_type = std::string(kFinalBlobSizeMismatch),
                    .resource_id = resource_id,
                    .resource_locator = storage_path,
                    .severity = ReconciliationSeverity::Critical,
                    .resolution_strategy = ResolutionStrategy::Manual,
                    .details = std::move(details),
                });
                ++page.findings_recorded;
            } else {
                co_await ResolveFinding(kFinalBlobSizeMismatch, resource_id);
            }
        }
        page.has_more = rows.size() == request.limit;
        co_return page;
    }

    auto StorageReconciliationService::RunUserPage(
        const ReconciliationPageRequest& request
    ) const -> drogon::Task<Result<ReconciliationPageResult>> {
        auto rows = co_await m_db_client->execSqlCoro(
            "SELECT users.id, users.storage_used, users.storage_reserved, " "       COALESCE((SELECT SUM(size) FROM files " "                 WHERE user_id = users.id), 0) AS active_file_bytes, " "       COALESCE((SELECT SUM(item_size) FROM trash " "                 WHERE user_id = users.id), 0) AS trash_item_bytes, " "       COALESCE((SELECT SUM(reserved_bytes) FROM upload_tasks " "                 WHERE user_id = users.id AND status IN (0, 4)), 0) " "           AS active_upload_reserved_bytes " "FROM users WHERE users.id > $1 ORDER BY users.id LIMIT $2",
            static_cast<int64_t>(request.after_id),
            static_cast<int64_t>(request.limit)
        );

        ReconciliationPageResult page;
        page.inspected = rows.size();
        for (const auto& row : rows) {
            const auto user_id = row["id"].as<uint64_t>();
            const auto resource_id = std::to_string(user_id);
            const auto storage_used = row["storage_used"].as<uint64_t>();
            const auto storage_reserved = row["storage_reserved"].as<uint64_t>();
            const auto active_file_bytes = row["active_file_bytes"].as<uint64_t>();
            const auto trash_item_bytes = row["trash_item_bytes"].as<uint64_t>();
            const auto active_upload_reserved =
                row["active_upload_reserved_bytes"].as<uint64_t>();
            const auto expected_used = AddWithoutOverflow(active_file_bytes, trash_item_bytes);
            page.next_after_id = user_id;

            if (storage_used != expected_used) {
                auto details = MakeDetails(request.scan_id);
                details["storage_used"] = Json::UInt64(storage_used);
                details["expected_used"] = Json::UInt64(expected_used);
                details["active_file_bytes"] = Json::UInt64(active_file_bytes);
                details["trash_item_bytes"] = Json::UInt64(trash_item_bytes);
                co_await RecordFinding(ReconciliationFinding{
                    .finding_type = std::string(kQuotaUsedMismatch),
                    .resource_id = resource_id,
                    .resource_locator = "users/" + resource_id,
                    .severity = ReconciliationSeverity::Warning,
                    .resolution_strategy = ResolutionStrategy::Alert,
                    .details = std::move(details),
                });
                ++page.findings_recorded;
            } else {
                co_await ResolveFinding(kQuotaUsedMismatch, resource_id);
            }

            if (storage_reserved != active_upload_reserved) {
                auto details = MakeDetails(request.scan_id);
                details["storage_reserved"] = Json::UInt64(storage_reserved);
                details["active_upload_reserved_bytes"] =
                    Json::UInt64(active_upload_reserved);
                co_await RecordFinding(ReconciliationFinding{
                    .finding_type = std::string(kQuotaReservedMismatch),
                    .resource_id = resource_id,
                    .resource_locator = "users/" + resource_id,
                    .severity = ReconciliationSeverity::Warning,
                    .resolution_strategy = ResolutionStrategy::Alert,
                    .details = std::move(details),
                });
                ++page.findings_recorded;
            } else {
                co_await ResolveFinding(kQuotaReservedMismatch, resource_id);
            }
        }
        page.has_more = rows.size() == request.limit;
        co_return page;
    }

    auto StorageReconciliationService::RunObjectPage(
        const ReconciliationPageRequest& request
    ) const -> drogon::Task<Result<ReconciliationPageResult>> {
        Result<disk::storage::StorageInventoryPage> inventory = std::unexpected(
            ErrorInfo(ErrorCode::InternalError, "Reconciliation inventory is not configured")
        );
        if (request.scope == ReconciliationScope::Staging) {
            if (m_staging_storage == nullptr) {
                co_return std::unexpected(inventory.error());
            }
            inventory = co_await m_staging_storage->ListStagingObjects(
                request.continuation_token,
                request.limit
            );
        } else {
            if (m_blob_store == nullptr) {
                co_return std::unexpected(inventory.error());
            }
            inventory = co_await m_blob_store->ListFinalObjects(
                request.continuation_token,
                request.limit
            );
        }
        if (!inventory) {
            co_return std::unexpected(inventory.error());
        }

        Json::Value locator_array(Json::arrayValue);
        for (const auto& object : inventory->objects) {
            locator_array.append(object.locator);
        }

        std::unordered_map<std::string, bool> referenced;
        if (!inventory->objects.empty()) {
            const auto serialized_locators = SerializeJson(locator_array);
            if (request.scope == ReconciliationScope::Staging) {
                auto rows = co_await m_db_client->execSqlCoro(
                    "SELECT inventory.locator, (" "  EXISTS (" "    SELECT 1 FROM upload_task_chunks AS chunk " "    JOIN upload_tasks AS task ON task.id = chunk.task_id " "    WHERE task.status IN (0, 4) AND (" "      chunk.object_key = inventory.locator OR (" "        task.staging_backend = 'local' AND chunk.object_key IS NULL " "        AND inventory.locator = task.id || '/' || " "            chunk.chunk_index::text || '.chunk'" "      )" "    )" "  ) OR EXISTS (" "    SELECT 1 FROM upload_tasks AS task " "    WHERE task.status = 4 AND (" "      (task.staging_backend = 's3' AND " "       inventory.locator LIKE task.staging_prefix || '/assembled/%') OR " "      (task.staging_backend = 'local' AND " "       inventory.locator = task.id || '.tmp')" "    )" "  )" ") AS referenced " "FROM jsonb_array_elements_text($1::jsonb) AS inventory(locator)",
                    serialized_locators
                );
                referenced.reserve(rows.size());
                for (const auto& row : rows) {
                    referenced.emplace(
                        row["locator"].as<std::string>(),
                        row["referenced"].as<bool>()
                    );
                }
            } else {
                auto rows = co_await m_db_client->execSqlCoro(
                    "SELECT inventory.locator, EXISTS (" "  SELECT 1 FROM file_contents " "  WHERE storage_path = inventory.locator" ") AS referenced " "FROM jsonb_array_elements_text($1::jsonb) AS inventory(locator)",
                    serialized_locators
                );
                referenced.reserve(rows.size());
                for (const auto& row : rows) {
                    referenced.emplace(
                        row["locator"].as<std::string>(),
                        row["referenced"].as<bool>()
                    );
                }
            }
        }

        ReconciliationPageResult page;
        page.inspected = inventory->objects.size();
        page.has_more = inventory->has_more;
        page.next_continuation_token = inventory->continuation_token;
        const auto finding_type = request.scope == ReconciliationScope::Staging ? kOrphanStagingObject : kOrphanFinalBlob;
        const auto severity = request.scope == ReconciliationScope::Staging ? ReconciliationSeverity::Warning : ReconciliationSeverity::Critical;
        const auto strategy = request.scope == ReconciliationScope::Staging ? ResolutionStrategy::Alert : ResolutionStrategy::Manual;

        for (const auto& object : inventory->objects) {
            const auto resource_id = BuildObjectResourceId(object.locator);
            if (referenced[object.locator]) {
                co_await ResolveFinding(finding_type, resource_id);
                continue;
            }

            auto details = MakeDetails(request.scan_id);
            details["size_bytes"] = Json::UInt64(object.size_bytes);
            co_await RecordFinding(ReconciliationFinding{
                .finding_type = std::string(finding_type),
                .resource_id = resource_id,
                .resource_locator = object.locator,
                .severity = severity,
                .resolution_strategy = strategy,
                .details = std::move(details),
            });
            ++page.findings_recorded;
        }
        co_return page;
    }

} // namespace disk::reconciliation
