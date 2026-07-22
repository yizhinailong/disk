/**
 * @file StorageJobAdminService.cpp
 * @brief Audited administrator operations for persistent storage jobs
 */

#include "services/StorageJobAdminService.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "services/StorageReconciliationService.hpp"
#include "services/TransactionRunner.hpp"
#include "utils/LogHelper.hpp"

namespace disk::jobs {
    namespace {
        constexpr std::string_view kJobProjection =
            "id, job_type, aggregate_id, dedupe_key, payload::text AS payload_json, " "status, attempts, max_attempts, available_at, locked_by, locked_until, " "last_error, created_at, updated_at, completed_at";

        [[nodiscard]] auto ParsePayload(std::string_view value) -> Json::Value {
            Json::CharReaderBuilder builder;
            Json::Value payload;
            std::string errors;
            std::istringstream input{ std::string(value) };
            if (!Json::parseFromStream(builder, input, &payload, &errors) ||
                !payload.isObject()) {
                throw std::runtime_error("Storage job payload is not a JSON object");
            }
            return payload;
        }

        template <typename Row>
        [[nodiscard]] auto OptionalString(const Row& row, const char* name)
            -> std::optional<std::string> {
            return row[name].isNull() ? std::nullopt :
                                        std::optional(row[name].template as<std::string>());
        }

        template <typename Row>
        [[nodiscard]] auto ToStorageJobItem(const Row& row, bool include_payload = true)
            -> disk::admin::StorageJobItem {
            const auto status = ParseStorageJobStatus(row["status"].template as<int16_t>());
            if (!status.has_value()) {
                throw std::runtime_error("Storage job has an invalid status");
            }
            return disk::admin::StorageJobItem{
                .id = row["id"].template as<uint64_t>(),
                .job_type = row["job_type"].template as<std::string>(),
                .aggregate_id = row["aggregate_id"].template as<std::string>(),
                .dedupe_key = row["dedupe_key"].template as<std::string>(),
                .payload = include_payload ?
                               ParsePayload(row["payload_json"].template as<std::string>()) :
                               Json::Value(Json::objectValue),
                .status = status.value(),
                .attempts = row["attempts"].template as<uint32_t>(),
                .max_attempts = row["max_attempts"].template as<uint32_t>(),
                .available_at = row["available_at"].template as<std::string>(),
                .locked_by = OptionalString(row, "locked_by"),
                .locked_until = OptionalString(row, "locked_until"),
                .last_error = OptionalString(row, "last_error"),
                .created_at = row["created_at"].template as<std::string>(),
                .updated_at = row["updated_at"].template as<std::string>(),
                .completed_at = OptionalString(row, "completed_at"),
            };
        }

        [[nodiscard]] auto SerializeJson(const Json::Value& value) -> std::string {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            return Json::writeString(builder, value);
        }

        auto SetLogContext(
            Json::Value& details,
            const disk::utils::LogContext& log_context
        ) -> void {
            details["request_id"] =
                log_context.request_id.has_value() && !log_context.request_id->empty() ?
                    Json::Value(*log_context.request_id) :
                    Json::Value(Json::nullValue);
            details["operation"] =
                log_context.operation.has_value() && !log_context.operation->empty() ?
                    Json::Value(*log_context.operation) :
                    Json::Value(Json::nullValue);
        }

        [[nodiscard]] auto Bounded(std::string value, size_t maximum) -> std::string {
            if (value.size() > maximum) {
                value.resize(maximum);
            }
            return value;
        }

        [[nodiscard]] auto ReplayInTransaction(
            const drogon::orm::DbClientPtr& client,
            uint64_t job_id,
            const disk::admin::StorageJobReplayRequest& request,
            const StorageJobAuditContext& audit,
            const disk::utils::LogContext& log_context
        ) -> drogon::Task<Result<disk::admin::StorageJobItem>> {
            const auto select_sql = std::string("SELECT ") + std::string(kJobProjection) +
                                    " FROM storage_jobs WHERE id = $1 FOR UPDATE";
            auto selected = co_await client->execSqlCoro(
                select_sql,
                static_cast<int64_t>(job_id)
            );
            if (selected.empty()) {
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::ResourceNotFound,
                    "Storage job not found"
                ));
            }
            const auto current = ToStorageJobItem(selected[0]);
            if (current.status != StorageJobStatus::DeadLetter) {
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::ResourceConflict,
                    "Storage job is not in dead-letter state"
                ));
            }

            const auto update_sql = std::string("UPDATE storage_jobs SET ") +
                                    "status = $1, attempts = 0, available_at = NOW(), " "locked_by = NULL, locked_until = NULL, last_error = NULL, " "completed_at = NULL, updated_at = NOW() " "WHERE id = $2 AND status = $3 RETURNING " +
                                    std::string(kJobProjection);
            auto updated = co_await client->execSqlCoro(
                update_sql,
                ToStorageValue(StorageJobStatus::Pending),
                static_cast<int64_t>(job_id),
                ToStorageValue(StorageJobStatus::DeadLetter)
            );
            if (updated.size() != 1) {
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::ResourceConflict,
                    "Storage job replay lost a concurrent race"
                ));
            }
            auto replayed_job = ToStorageJobItem(updated[0]);

            Json::Value details(Json::objectValue);
            details["job_id"] = Json::UInt64(job_id);
            details["job_type"] = current.job_type;
            details["dedupe_key"] = current.dedupe_key;
            details["previous_status"] = std::string(StorageJobStatusName(current.status));
            details["previous_attempts"] = current.attempts;
            details["reason"] = request.reason;
            SetLogContext(details, log_context);
            auto inserted = co_await client->execSqlCoro(
                "INSERT INTO operation_logs " "(user_id, action, target_type, target_id, target_name, details, ip_address, user_agent) " "VALUES ($1, 'admin.storage_job.replay', 'storage_job', $2, $3, $4::jsonb, $5, NULLIF($6, ''))",
                static_cast<int64_t>(audit.operator_id),
                static_cast<int64_t>(job_id),
                current.dedupe_key,
                SerializeJson(details),
                Bounded(audit.ip_address.empty() ? "unknown" : audit.ip_address, 45),
                Bounded(audit.user_agent, 512)
            );
            if (inserted.affectedRows() != 1) {
                throw std::runtime_error("Storage job replay audit was not inserted");
            }
            co_return replayed_job;
        }
    } // namespace

    StorageJobAdminService::StorageJobAdminService(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
        if (m_db_client == nullptr) {
            throw std::invalid_argument("Storage job admin database client is required");
        }
    }

    auto StorageJobAdminService::List(
        const disk::admin::StorageJobListRequest& request,
        disk::utils::LogContext log_context
    ) const
        -> drogon::Task<Result<disk::admin::StorageJobListResponse>> {
        try {
            const auto status = ToStorageValue(request.status);
            const auto job_type = request.job_type.value_or("");
            const auto offset = static_cast<int64_t>(request.page - 1) * request.page_size;
            const auto projection = std::string("SELECT ") + std::string(kJobProjection) +
                                    " FROM storage_jobs";
            auto count = co_await m_db_client->execSqlCoro(
                "SELECT COUNT(*) AS total FROM storage_jobs " "WHERE status = $1 AND ($2 = '' OR job_type = $2)",
                status,
                job_type
            );
            auto rows = co_await m_db_client->execSqlCoro(
                projection +
                    " WHERE status = $1 AND ($2 = '' OR job_type = $2) " "ORDER BY updated_at DESC, id DESC LIMIT $3 OFFSET $4",
                status,
                job_type,
                static_cast<int64_t>(request.page_size),
                offset
            );

            disk::admin::StorageJobListResponse response{
                .page = request.page,
                .page_size = request.page_size,
            };
            response.total = count.empty() ? 0 : count[0]["total"].as<uint64_t>();
            response.total_pages = response.total == 0 ? 0 :
                                                         (response.total + request.page_size - 1) /
                                                             static_cast<uint64_t>(request.page_size);
            response.items.reserve(rows.size());
            for (const auto& row : rows) {
                response.items.push_back(ToStorageJobItem(row));
            }
            co_return response;
        } catch (const std::exception& error) {
            Logger::Error(log_context)
                << "Storage job admin list failed: error=" << error.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to list storage jobs"
            ));
        }
    }

    auto StorageJobAdminService::Get(
        uint64_t job_id,
        disk::utils::LogContext log_context
    ) const
        -> drogon::Task<Result<disk::admin::StorageJobItem>> {
        log_context.job_id = job_id;
        try {
            const auto sql = std::string("SELECT ") + std::string(kJobProjection) +
                             " FROM storage_jobs WHERE id = $1";
            auto rows = co_await m_db_client->execSqlCoro(sql, static_cast<int64_t>(job_id));
            if (rows.empty()) {
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::ResourceNotFound,
                    "Storage job not found"
                ));
            }
            co_return ToStorageJobItem(rows[0]);
        } catch (const std::exception& error) {
            Logger::Error(log_context)
                << "Storage job admin detail failed: job_id=" << job_id
                << ", error=" << error.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to get storage job"
            ));
        }
    }

    auto StorageJobAdminService::ListRelatedToUpload(
        const std::string& upload_id,
        const std::string& staging_prefix,
        int page,
        int page_size,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<Result<disk::admin::StorageJobListResponse>> {
        try {
            constexpr std::string_view relation =
                "((job_type = $1 AND aggregate_id = $2) " "OR ($4 <> '' AND job_type = $3 AND " "((payload ->> 'key') = $4 OR " "LEFT(payload ->> 'key', CHAR_LENGTH($4) + 1) = $4 || '/')) " "OR (job_type = $5 AND aggregate_id IN " "(SELECT details ->> 'scan_id' FROM storage_reconciliation_findings " "WHERE finding_type = $6 AND resource_id = $2 AND details ? 'scan_id')))";
            const auto offset = static_cast<int64_t>(page - 1) * page_size;
            const auto cleanup_type = std::string(kStagingCleanupJobType);
            const auto multipart_abort_type = std::string(kMultipartAbortJobType);
            const auto reconciliation_type = std::string(kStorageReconcileJobType);
            const auto finding_type =
                std::string(disk::reconciliation::kUploadStagingMismatchFindingType);

            auto count = co_await m_db_client->execSqlCoro(
                std::string("SELECT COUNT(*) AS total FROM storage_jobs WHERE ") +
                    std::string(relation),
                cleanup_type,
                upload_id,
                multipart_abort_type,
                staging_prefix,
                reconciliation_type,
                finding_type
            );
            auto rows = co_await m_db_client->execSqlCoro(
                std::string("SELECT ") + std::string(kJobProjection) +
                    " FROM storage_jobs WHERE " + std::string(relation) +
                    " ORDER BY updated_at DESC, id DESC LIMIT $7 OFFSET $8",
                cleanup_type,
                upload_id,
                multipart_abort_type,
                staging_prefix,
                reconciliation_type,
                finding_type,
                static_cast<int64_t>(page_size),
                offset
            );

            disk::admin::StorageJobListResponse response{
                .page = page,
                .page_size = page_size,
            };
            response.total = count.empty() ? 0 : count[0]["total"].as<uint64_t>();
            response.total_pages = response.total == 0 ? 0 :
                                                         (response.total + page_size - 1) /
                                                             static_cast<uint64_t>(page_size);
            response.items.reserve(rows.size());
            for (const auto& row : rows) {
                auto item = ToStorageJobItem(row, false);
                item.last_error.reset();
                response.items.push_back(std::move(item));
            }
            co_return response;
        } catch (const std::exception&) {
            Logger::Error(log_context) << "Related upload storage job list failed";
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to list related storage jobs"
            ));
        }
    }

    auto StorageJobAdminService::Replay(
        uint64_t job_id,
        const disk::admin::StorageJobReplayRequest& request,
        const StorageJobAuditContext& audit,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<Result<disk::admin::StorageJobReplayResponse>> {
        log_context.job_id = job_id;
        if (request.dry_run) {
            auto job = co_await Get(job_id, log_context);
            if (!job) {
                co_return std::unexpected(job.error());
            }
            const auto eligible = job->status == StorageJobStatus::DeadLetter;
            co_return disk::admin::StorageJobReplayResponse{
                .job = std::move(job.value()),
                .dry_run = true,
                .eligible = eligible,
                .replayed = false,
            };
        }

        if (audit.operator_id == 0 || !request.confirm_job_id.has_value() ||
            request.confirm_job_id.value() != job_id || request.reason.empty()) {
            co_return std::unexpected(ErrorInfo(
                ErrorCode::ValidationFailed,
                "Storage job replay confirmation is incomplete"
            ));
        }

        disk::admin::StorageJobItem replayed_job;
        disk::file::TransactionRunner transaction(
            m_db_client,
            ErrorInfo(ErrorCode::InternalError, "Failed to replay storage job"),
            log_context
        );
        auto result = co_await transaction.Run(
            [&](const drogon::orm::DbClientPtr& client) -> drogon::Task<Result<void>> {
                auto replay = co_await ReplayInTransaction(
                    client,
                    job_id,
                    request,
                    audit,
                    log_context
                );
                if (!replay) {
                    co_return std::unexpected(replay.error());
                }
                replayed_job = std::move(replay.value());
                co_return {};
            }
        );
        if (!result) {
            co_return std::unexpected(result.error());
        }

        Logger::Info(log_context)
            << "Storage job dead-letter replayed: job_id=" << job_id
            << ", operator_id=" << audit.operator_id;
        co_return disk::admin::StorageJobReplayResponse{
            .job = std::move(replayed_job),
            .dry_run = false,
            .eligible = true,
            .replayed = true,
        };
    }

} // namespace disk::jobs
