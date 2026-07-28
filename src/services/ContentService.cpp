/**
 * @file ContentService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件内容领域服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "ContentService.hpp"

#include <algorithm>
#include <utility>

#include <drogon/orm/CoroMapper.h>
#include <drogon/orm/Criteria.h>

#include "models/FileContents.hpp"
#include "services/FileServiceUtils.hpp"
#include "services/StorageJobRepository.hpp"
#include "utils/BatchUtils.hpp"
#include "utils/LogHelper.hpp"

namespace disk::content {

    using disk::utils::BatchUtils;
    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::FileContents;

    namespace {

        auto ToMetadata(const FileContents& content) -> ContentMetadata {
            return ContentMetadata{ .id = static_cast<uint64_t>(content.getValueOfId()),
                                    .hash_md5 = content.getValueOfHashMd5(),
                                    .hash_sha256 = content.getValueOfHashSha256(),
                                    .size = static_cast<uint64_t>(content.getValueOfSize()),
                                    .storage_path = content.getValueOfStoragePath(),
                                    .mime_type = content.getValueOfMimeType(),
                                    .ref_count = static_cast<int>(content.getValueOfRefCount()) };
        }

        auto CollectContentIds(const std::unordered_map<uint64_t, uint64_t>& deltas)
            -> std::vector<uint64_t> {
            std::vector<uint64_t> content_ids;
            content_ids.reserve(deltas.size());
            for (const auto& [content_id, delta] : deltas) {
                if (delta == 0) {
                    continue;
                }
                content_ids.push_back(content_id);
            }
            return content_ids;
        }

        struct ZeroRefContent {
            uint64_t id{ 0 };
            std::string storage_path;
        };

    } // namespace

    ContentService::ContentService(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
        Logger::Debug(disk::utils::ServiceRuntimeLogContext()) << "Service initialized: service=content";
    }

    auto ContentService::FindByMd5(
        const std::string& hash_md5,
        disk::utils::LogContext log_context
    ) const
        -> drogon::Task<std::optional<ContentMetadata>> {
        co_return co_await FindByMd5(m_db_client, hash_md5, log_context);
    }

    auto ContentService::FindByMd5(
        const drogon::orm::DbClientPtr& client,
        const std::string& hash_md5,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<std::optional<ContentMetadata>> {
        try {
            CoroMapper<FileContents> mapper(client);
            auto content = co_await mapper.findOne(
                Criteria(FileContents::Cols::_hash_md5, CompareOperator::EQ, hash_md5)
            );
            co_return ToMetadata(content);
        } catch (const drogon::orm::UnexpectedRows&) {
            co_return std::nullopt;
        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Warn(log_context) << "File content lookup failed";
            co_return std::nullopt;
        }
    }

    auto ContentService::FindExistingIds(
        const drogon::orm::DbClientPtr& client,
        const std::vector<uint64_t>& content_ids,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<std::unordered_set<uint64_t>> {
        std::unordered_set<uint64_t> existing_ids;
        if (content_ids.empty()) {
            co_return existing_ids;
        }

        try {
            auto rows = co_await client->execSqlCoro(
                "SELECT id FROM file_contents WHERE id IN (" +
                BatchUtils::BuildSafeNumericInClause(content_ids) + ")"
            );
            existing_ids.reserve(rows.size());
            for (const auto& row : rows) {
                existing_ids.insert(row["id"].as<uint64_t>());
            }
        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Warn(log_context) << "File content batch ID lookup failed";
        }

        co_return existing_ids;
    }

    auto ContentService::AcquireReference(
        const drogon::orm::DbClientPtr& client,
        const NewContent& content,
        std::optional<uint64_t> expected_existing_content_id,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<Result<ContentMetadata>> {
        try {
            auto existing = co_await client->execSqlCoro(
                "SELECT id, size FROM file_contents " "WHERE hash_md5 = $1 AND hash_sha256 = $2 FOR UPDATE",
                content.hash_md5,
                content.hash_sha256
            );
            if (expected_existing_content_id.has_value()) {
                if (existing.empty() ||
                    existing[0]["id"].as<uint64_t>() != expected_existing_content_id.value()) {
                    co_return std::unexpected(ErrorInfo(
                        ErrorCode::ResourceConflict,
                        "File content changed while finalizing upload"
                    ));
                }
            }
            if (!existing.empty()) {
                if (existing[0]["size"].as<uint64_t>() != content.size) {
                    co_return std::unexpected(ErrorInfo(
                        ErrorCode::ChunkVerifyFailed,
                        "Existing file content size does not match its hashes"
                    ));
                }
                auto gate_result = co_await CheckReferenceGate(
                    client,
                    existing[0]["id"].as<uint64_t>(),
                    log_context
                );
                if (!gate_result) {
                    co_return std::unexpected(gate_result.error());
                }
            }

            auto result = co_await client->execSqlCoro(
                "INSERT INTO file_contents " "  (hash_md5, hash_sha256, size, storage_path, mime_type, ref_count) " "VALUES ($1, $2, $3, $4, $5, 1) " "ON CONFLICT (hash_md5, hash_sha256) DO UPDATE SET " "  ref_count = file_contents.ref_count + 1 " "WHERE file_contents.size = EXCLUDED.size " "RETURNING *",
                content.hash_md5,
                content.hash_sha256,
                content.size,
                content.storage_path,
                content.mime_type
            );
            if (result.empty()) {
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::ChunkVerifyFailed,
                    "Existing file content size does not match its hashes"
                ));
            }

            auto metadata = ToMetadata(FileContents(result[0], -1));
            if (expected_existing_content_id.has_value() &&
                metadata.id != expected_existing_content_id.value()) {
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::ResourceConflict,
                    "File content changed while finalizing upload"
                ));
            }
            if (existing.empty()) {
                auto gate_result = co_await CheckReferenceGate(client, metadata.id, log_context);
                if (!gate_result) {
                    co_return std::unexpected(gate_result.error());
                }
            }

            co_return metadata;
        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Error(log_context) << "File content reference acquisition failed";
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to acquire file content reference")
            );
        }
    }

    auto ContentService::IncrementRefCount(
        const drogon::orm::DbClientPtr& client,
        uint64_t content_id,
        uint64_t increment,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<Result<void>> {
        if (increment == 0) {
            co_return {};
        }

        try {
            auto locked = co_await client->execSqlCoro(
                "SELECT id FROM file_contents WHERE id = $1 FOR UPDATE",
                static_cast<int64_t>(content_id)
            );
            if (locked.empty()) {
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::ResourceConflict,
                    "File content changed while acquiring a reference"
                ));
            }

            auto gate_result = co_await CheckReferenceGate(client, content_id, log_context);
            if (!gate_result) {
                co_return std::unexpected(gate_result.error());
            }

            auto result = co_await client->execSqlCoro(
                "UPDATE file_contents SET ref_count = ref_count + $1 WHERE id = $2",
                static_cast<int32_t>(increment),
                static_cast<int64_t>(content_id)
            );
            if (result.affectedRows() == 0) {
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::ResourceConflict,
                    "File content changed while acquiring a reference"
                ));
            }
            co_return {};
        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Error(log_context) << "File content reference increment failed";
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to update file content reference count")
            );
        }
    }

    auto ContentService::IncrementRefCountsChecked(
        const drogon::orm::DbClientPtr& client,
        const std::unordered_map<uint64_t, uint64_t>& increments,
        const std::unordered_set<uint64_t>& existing_content_ids,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<Result<std::unordered_set<uint64_t>>> {
        std::string update_sql = "UPDATE file_contents SET ref_count = ref_count + CASE id";
        std::vector<uint64_t> valid_content_ids;
        valid_content_ids.reserve(increments.size());

        for (const auto& [content_id, increment] : increments) {
            if (increment == 0 || !existing_content_ids.contains(content_id)) {
                continue;
            }
            valid_content_ids.push_back(content_id);
            update_sql += " WHEN " + std::to_string(content_id) + " THEN " + std::to_string(increment);
        }

        std::unordered_set<uint64_t> incremented_ids;
        incremented_ids.reserve(valid_content_ids.size());

        if (valid_content_ids.empty()) {
            co_return incremented_ids;
        }

        std::sort(valid_content_ids.begin(), valid_content_ids.end());

        update_sql += " ELSE 0 END WHERE id IN (" +
                      BatchUtils::BuildSafeNumericInClause(valid_content_ids) + ")";

        try {
            auto locked = co_await client->execSqlCoro(
                "SELECT id FROM file_contents WHERE id IN (" +
                BatchUtils::BuildSafeNumericInClause(valid_content_ids) +
                ") ORDER BY id FOR UPDATE"
            );
            if (locked.size() != valid_content_ids.size()) {
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::ResourceConflict,
                    "File content changed while copying"
                ));
            }
            for (const auto content_id : valid_content_ids) {
                auto gate_result = co_await CheckReferenceGate(client, content_id, log_context);
                if (!gate_result) {
                    co_return std::unexpected(gate_result.error());
                }
            }

            auto result = co_await client->execSqlCoro(update_sql);
            if (result.affectedRows() != valid_content_ids.size()) {
                Logger::Warn(log_context)
                    << "File content batch reference increment row mismatch";
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to update file content reference counts")
                );
            }
            for (const auto id : valid_content_ids) {
                incremented_ids.insert(id);
            }
        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Warn(log_context) << "File content batch reference increment failed";
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to update file content reference counts")
            );
        }

        co_return incremented_ids;
    }

    auto ContentService::DecrementRefCountsAndEnqueueGc(
        const drogon::orm::DbClientPtr& client,
        const std::unordered_map<uint64_t, uint64_t>& decrements,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<Result<size_t>> {
        auto content_ids = CollectContentIds(decrements);
        if (content_ids.empty()) {
            co_return size_t{ 0 };
        }

        std::string update_sql =
            "UPDATE file_contents SET ref_count = GREATEST(ref_count - CASE id";
        std::vector<std::pair<uint64_t, uint64_t>> update_cases;
        update_cases.reserve(content_ids.size());

        int param_index = 1;
        for (const auto& [content_id, decrement] : decrements) {
            if (decrement == 0) {
                continue;
            }
            update_cases.emplace_back(content_id, decrement);
            auto when_param = std::to_string(param_index++);
            auto then_param = std::to_string(param_index++);
            update_sql += " WHEN $" + when_param + " THEN $" + then_param;
        }
        update_sql += " ELSE 0 END, 0) WHERE id IN (" +
                      BatchUtils::BuildSafeNumericInClause(content_ids) + ")";

        try {
            co_await disk::file::utils::ExecSqlWithBindings(
                client,
                update_sql,
                [&](auto& binder) {
                    for (const auto& [content_id, decrement] : update_cases) {
                        binder << static_cast<int64_t>(content_id)
                               << static_cast<int32_t>(decrement);
                    }
                }
            );
            auto rows = co_await client->execSqlCoro(
                "SELECT id, storage_path FROM file_contents WHERE ref_count = 0 AND id IN (" +
                BatchUtils::BuildSafeNumericInClause(content_ids) + ")"
            );
            std::vector<ZeroRefContent> zero_ref_contents;
            zero_ref_contents.reserve(rows.size());
            for (const auto& row : rows) {
                zero_ref_contents.push_back({ .id = row["id"].as<uint64_t>(), .storage_path = row["storage_path"].as<std::string>() });
            }

            disk::jobs::StorageJobRepository job_repository(m_db_client);
            for (const auto& content : zero_ref_contents) {
                Json::Value payload(Json::objectValue);
                payload["content_id"] = Json::UInt64(content.id);
                payload["storage_path"] = content.storage_path;
                const auto aggregate_id = std::to_string(content.id);
                (void)co_await job_repository.EnqueueOrRearmSucceeded(
                    client,
                    disk::jobs::NewStorageJob{
                        .job_type = std::string(disk::jobs::kBlobGcJobType),
                        .aggregate_id = aggregate_id,
                        .dedupe_key = "blob-gc:" + aggregate_id,
                        .payload = std::move(payload),
                    }
                );
            }

            co_return zero_ref_contents.size();
        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Warn(log_context) << "File content decrement and Blob GC enqueue failed";
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to update file content references"
            ));
        } catch (const std::exception&) {
            Logger::Warn(log_context) << "Blob GC enqueue failed";
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to enqueue Blob garbage collection"
            ));
        }
    }

    auto ContentService::CheckReferenceGate(
        const drogon::orm::DbClientPtr& client,
        uint64_t content_id,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<Result<void>> {
        try {
            disk::jobs::StorageJobRepository repository(m_db_client);
            auto gate = co_await repository.CheckBlobGcReferenceGate(client, content_id);
            if (gate == disk::jobs::BlobGcReferenceGate::Blocked) {
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::ResourceConflict,
                    "File content is pending garbage collection"
                ));
            }
            co_return {};
        } catch (const std::exception&) {
            Logger::Warn(log_context) << "Blob GC reference gate failed";
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to verify file content lifecycle"
            ));
        }
    }

} // namespace disk::content
