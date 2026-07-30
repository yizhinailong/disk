/**
 * @file UploadTaskRepository.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 上传任务持久化原语实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "UploadTaskRepository.hpp"

#include <stdexcept>
#include <utility>

#include <drogon/orm/Mapper.h>

namespace disk::file {

    namespace {
        constexpr size_t kMaxLeaseOwnerLength = 128;

        auto ValidateLeaseArguments(
            const std::string& lease_owner,
            uint32_t lease_duration_seconds
        ) -> void {
            if (lease_owner.empty() || lease_owner.size() > kMaxLeaseOwnerLength) {
                throw std::invalid_argument("Finalize lease owner must contain 1 to 128 characters");
            }
            if (lease_duration_seconds == 0) {
                throw std::invalid_argument("Finalize lease duration must be positive");
            }
        }

        template <typename Row>
        [[nodiscard]] auto ToUploadTaskCleanupRecord(const Row& row)
            -> UploadTaskCleanupRecord {
            const auto backend = disk::storage::ParseUploadStagingBackend(
                row["staging_backend"].template as<std::string>()
            );
            if (!backend.has_value()) {
                throw std::runtime_error("Upload task contains an unsupported staging backend");
            }

            const auto upload_id = row["id"].template as<std::string>();
            return UploadTaskCleanupRecord{
                .id = upload_id,
                .temp_path = row["temp_path"].template as<std::string>(),
                .user_id = row["user_id"].template as<uint64_t>(),
                .reserved_bytes = row["reserved_bytes"].template as<uint64_t>(),
                .staging_session = disk::storage::UploadStagingSession{
                                                                       .upload_id = upload_id,
                                                                       .backend = backend.value(),
                                                                       .prefix = row["staging_prefix"].template as<std::string>(),
                                                                       },
            };
        }
    } // namespace

    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::UploadTasks;

    UploadTaskRepository::UploadTaskRepository(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
    }

    auto UploadTaskRepository::FindByIdForUser(const std::string& upload_id, uint64_t user_id) const
        -> drogon::Task<std::optional<UploadTasks>> {
        try {
            CoroMapper<UploadTasks> mapper(m_db_client);
            co_return co_await mapper.findOne(
                Criteria(UploadTasks::Cols::_id, CompareOperator::EQ, upload_id) &&
                Criteria(UploadTasks::Cols::_user_id, CompareOperator::EQ, user_id)
            );
        } catch (const drogon::orm::DrogonDbException&) {
            co_return std::nullopt;
        }
    }

    auto UploadTaskRepository::FindUnexpiredByIdForUser(
        const std::string& upload_id,
        uint64_t user_id
    ) const -> drogon::Task<std::optional<UploadTasks>> {
        auto result = co_await m_db_client->execSqlCoro(
            "SELECT * FROM upload_tasks " "WHERE id = $1 AND user_id = $2 AND expires_at >= NOW()",
            upload_id,
            user_id
        );
        if (result.empty()) {
            co_return std::nullopt;
        }
        co_return UploadTasks(result[0], -1);
    }

    auto UploadTaskRepository::FindInProgressByUserAndHash(
        uint64_t user_id,
        const std::string& file_hash
    ) const -> drogon::Task<std::optional<UploadTasks>> {
        try {
            CoroMapper<UploadTasks> mapper(m_db_client);
            co_return co_await mapper.findOne(
                Criteria(UploadTasks::Cols::_user_id, CompareOperator::EQ, user_id) &&
                Criteria(UploadTasks::Cols::_file_hash, CompareOperator::EQ, file_hash) &&
                Criteria(
                    UploadTasks::Cols::_status,
                    CompareOperator::EQ,
                    disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::InProgress)
                )
            );
        } catch (const drogon::orm::DrogonDbException&) {
            co_return std::nullopt;
        }
    }

    auto UploadTaskRepository::FindInProgressIdByUserAndHash(
        uint64_t user_id,
        const std::string& file_hash
    ) const -> drogon::Task<std::optional<std::string>> {
        auto result = co_await m_db_client->execSqlCoro(
            "SELECT id FROM upload_tasks " "WHERE user_id = $1 AND file_hash = $2 AND status = $3 LIMIT 1",
            user_id,
            file_hash,
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::InProgress)
        );

        if (result.empty()) {
            co_return std::nullopt;
        }

        co_return result[0]["id"].as<std::string>();
    }

    auto UploadTaskRepository::Create(
        UploadTasks task,
        const disk::storage::UploadStagingSession& staging_session,
        uint32_t expiry_seconds
    ) const -> drogon::Task<UploadTasks> {
        if (staging_session.upload_id != task.getValueOfId()) {
            throw std::invalid_argument("Staging session upload ID must match upload task ID");
        }
        if (staging_session.prefix.empty()) {
            throw std::invalid_argument("Staging session prefix must not be empty");
        }

        auto result = co_await m_db_client->execSqlCoro(
            "INSERT INTO upload_tasks (" "id, user_id, folder_id, filename, file_size, file_hash, chunk_size, total_chunks, " "reserved_bytes, temp_path, staging_backend, staging_prefix, status, expires_at" ") VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, " "NOW() + ($14::integer * INTERVAL '1 second')) RETURNING *",
            task.getValueOfId(),
            task.getValueOfUserId(),
            task.getValueOfFolderId(),
            task.getValueOfFilename(),
            task.getValueOfFileSize(),
            task.getValueOfFileHash(),
            task.getValueOfChunkSize(),
            task.getValueOfTotalChunks(),
            task.getValueOfReservedBytes(),
            task.getValueOfTempPath(),
            std::string(disk::storage::ToStorageValue(staging_session.backend)),
            staging_session.prefix,
            static_cast<int16_t>(task.getValueOfStatus()),
            static_cast<int32_t>(expiry_seconds)
        );

        co_return UploadTasks(result[0], -1);
    }

    auto UploadTaskRepository::FindStagingSessionForUser(
        const std::string& upload_id,
        uint64_t user_id
    ) const -> drogon::Task<std::optional<disk::storage::UploadStagingSession>> {
        auto result = co_await m_db_client->execSqlCoro(
            "SELECT id, staging_backend, COALESCE(staging_prefix, temp_path) AS staging_prefix " "FROM upload_tasks WHERE id = $1 AND user_id = $2",
            upload_id,
            user_id
        );
        if (result.empty()) {
            co_return std::nullopt;
        }

        const auto backend_value = result[0]["staging_backend"].as<std::string>();
        const auto backend = disk::storage::ParseUploadStagingBackend(backend_value);
        if (!backend.has_value()) {
            throw std::runtime_error("Upload task contains an unsupported staging backend");
        }

        co_return disk::storage::UploadStagingSession{
            .upload_id = result[0]["id"].as<std::string>(),
            .backend = backend.value(),
            .prefix = result[0]["staging_prefix"].as<std::string>(),
        };
    }

    auto UploadTaskRepository::ClaimFinalizeLease(
        const std::string& upload_id,
        uint64_t user_id,
        const std::string& lease_owner,
        uint32_t lease_duration_seconds
    ) const -> drogon::Task<FinalizeClaimResult> {
        ValidateLeaseArguments(lease_owner, lease_duration_seconds);

        auto claimed = co_await m_db_client->execSqlCoro(
            "UPDATE upload_tasks AS task SET " "status = $1, lease_owner = $2, " "lease_expires_at = NOW() + ($3::integer * INTERVAL '1 second'), " "state_version = state_version + 1, " "finalize_attempts = finalize_attempts + 1, " "last_error_code = NULL, last_error_at = NULL " "WHERE task.id = $4 AND task.user_id = $5 AND (" "  (task.status = $6 AND task.expires_at >= NOW() AND (" "    SELECT COUNT(*) = task.total_chunks " "       AND COALESCE(MAX(chunk.chunk_index), -1) = task.total_chunks - 1 " "    FROM upload_task_chunks AS chunk WHERE chunk.task_id = task.id" "  )) " "  OR (task.status = $1 AND task.lease_expires_at <= NOW())" ") " "RETURNING state_version, finalize_attempts",
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::Finalizing),
            lease_owner,
            static_cast<int32_t>(lease_duration_seconds),
            upload_id,
            user_id,
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::InProgress)
        );

        if (!claimed.empty()) {
            co_return FinalizeClaimResult{
                .disposition = FinalizeClaimDisposition::Acquired,
                .state_version = claimed[0]["state_version"].as<uint64_t>(),
                .finalize_attempts = claimed[0]["finalize_attempts"].as<uint32_t>(),
            };
        }

        auto current = co_await m_db_client->execSqlCoro(
            "SELECT status, state_version, finalize_attempts, completed_file_id, " "COALESCE(lease_expires_at <= NOW(), FALSE) AS lease_expired, " "expires_at < NOW() AS task_expired " "FROM upload_tasks WHERE id = $1 AND user_id = $2",
            upload_id,
            user_id
        );
        if (current.empty()) {
            co_return FinalizeClaimResult{ .disposition = FinalizeClaimDisposition::NotFound };
        }

        const auto& row = current[0];
        const auto status = row["status"].as<int>();
        const auto action = disk::upload::DecideFinalizeRequest(
            status,
            row["lease_expired"].as<bool>()
        );

        FinalizeClaimResult result{
            .state_version = row["state_version"].as<uint64_t>(),
            .finalize_attempts = row["finalize_attempts"].as<uint32_t>(),
        };
        if (!row["completed_file_id"].isNull()) {
            result.completed_file_id = row["completed_file_id"].as<uint64_t>();
        }

        switch (action) {
            case disk::upload::FinalizeRequestAction::ClaimLease:
                result.disposition = row["task_expired"].as<bool>() ? FinalizeClaimDisposition::Terminal : FinalizeClaimDisposition::IncompleteChunks;
                break;
            case disk::upload::FinalizeRequestAction::TakeOverExpiredLease:
            case disk::upload::FinalizeRequestAction::RetryLater:
                result.disposition = FinalizeClaimDisposition::LeaseHeld;
                break;
            case disk::upload::FinalizeRequestAction::ReplayCompleted:
                result.disposition = FinalizeClaimDisposition::CompletedReplay;
                break;
            case disk::upload::FinalizeRequestAction::RejectTerminal:
                result.disposition = FinalizeClaimDisposition::Terminal;
                break;
        }

        co_return result;
    }

    auto UploadTaskRepository::RenewFinalizeLease(
        const drogon::orm::DbClientPtr& client,
        const std::string& upload_id,
        uint64_t user_id,
        const std::string& lease_owner,
        uint64_t expected_state_version,
        uint32_t lease_duration_seconds
    ) const -> drogon::Task<std::optional<uint64_t>> {
        ValidateLeaseArguments(lease_owner, lease_duration_seconds);

        auto result = co_await client->execSqlCoro(
            "UPDATE upload_tasks SET " "lease_expires_at = NOW() + ($1::integer * INTERVAL '1 second'), " "state_version = state_version + 1 " "WHERE id = $2 AND user_id = $3 AND status = $4 " "AND lease_owner = $5 AND state_version = $6 " "AND lease_expires_at > NOW() " "RETURNING state_version",
            static_cast<int32_t>(lease_duration_seconds),
            upload_id,
            user_id,
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::Finalizing),
            lease_owner,
            expected_state_version
        );
        if (result.empty()) {
            co_return std::nullopt;
        }
        co_return result[0]["state_version"].as<uint64_t>();
    }

    auto UploadTaskRepository::MarkCompletedIfLeaseOwned(
        const drogon::orm::DbClientPtr& client,
        const std::string& upload_id,
        uint64_t user_id,
        const std::string& lease_owner,
        uint64_t expected_state_version,
        uint64_t completed_file_id
    ) const -> drogon::Task<bool> {
        auto result = co_await client->execSqlCoro(
            "UPDATE upload_tasks SET status = $1, completed_file_id = $2, " "finalized_at = NOW(), lease_owner = NULL, lease_expires_at = NULL, " "state_version = state_version + 1 " "WHERE id = $3 AND user_id = $4 AND status = $5 " "AND lease_owner = $6 AND state_version = $7 " "AND lease_expires_at > NOW()",
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::Completed),
            completed_file_id,
            upload_id,
            user_id,
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::Finalizing),
            lease_owner,
            expected_state_version
        );
        co_return result.affectedRows() > 0;
    }

    auto UploadTaskRepository::RecordFinalizeErrorIfLeaseOwned(
        const std::string& upload_id,
        uint64_t user_id,
        const std::string& lease_owner,
        uint64_t expected_state_version,
        int error_code
    ) const -> drogon::Task<bool> {
        auto result = co_await m_db_client->execSqlCoro(
            "UPDATE upload_tasks SET last_error_code = $1, last_error_at = NOW() " "WHERE id = $2 AND user_id = $3 AND status = $4 " "AND lease_owner = $5 AND state_version = $6 " "AND lease_expires_at > NOW()",
            error_code,
            upload_id,
            user_id,
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::Finalizing),
            lease_owner,
            expected_state_version
        );
        co_return result.affectedRows() > 0;
    }

    auto UploadTaskRepository::MarkCancelledIfInProgressReturning(
        const drogon::orm::DbClientPtr& client,
        const std::string& upload_id,
        uint64_t user_id,
        const std::string& fail_reason
    ) const -> drogon::Task<std::optional<CancelledUploadTaskRecord>> {
        auto result = co_await client->execSqlCoro(
            "UPDATE upload_tasks SET status = $1, finalized_at = NOW(), fail_reason = $2, " "state_version = state_version + 1 " "WHERE id = $3 AND user_id = $4 AND status = $5 AND expires_at >= NOW() " "RETURNING id, temp_path, user_id, reserved_bytes, staging_backend, " "COALESCE(staging_prefix, temp_path) AS staging_prefix, state_version",
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::Cancelled),
            fail_reason,
            upload_id,
            user_id,
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::InProgress)
        );
        if (result.empty()) {
            co_return std::nullopt;
        }
        co_return CancelledUploadTaskRecord{
            .cleanup = ToUploadTaskCleanupRecord(result[0]),
            .state_version = result[0]["state_version"].as<uint64_t>(),
        };
    }

    auto UploadTaskRepository::FindCancellationStateByIdForUser(
        const std::string& upload_id,
        uint64_t user_id
    ) const -> drogon::Task<std::optional<UploadTaskCancellationState>> {
        auto result = co_await m_db_client->execSqlCoro(
            "SELECT status, state_version, expires_at < NOW() AS task_expired " "FROM upload_tasks WHERE id = $1 AND user_id = $2",
            upload_id,
            user_id
        );
        if (result.empty()) {
            co_return std::nullopt;
        }

        co_return UploadTaskCancellationState{
            .status = result[0]["status"].as<int>(),
            .state_version = result[0]["state_version"].as<uint64_t>(),
            .task_expired = result[0]["task_expired"].as<bool>(),
        };
    }

    auto UploadTaskRepository::MarkExpiredIfInProgressReturning(
        const drogon::orm::DbClientPtr& client,
        const std::string& upload_id,
        const std::string& fail_reason
    ) const -> drogon::Task<std::optional<ExpiredUploadTransitionRecord>> {
        auto result = co_await client->execSqlCoro(
            "UPDATE upload_tasks SET status = $1, finalized_at = NOW(), fail_reason = $2, " "state_version = state_version + 1 " "WHERE id = $3 AND status = $4 AND expires_at < NOW() " "RETURNING id, temp_path, user_id, reserved_bytes, staging_backend, " "COALESCE(staging_prefix, temp_path) AS staging_prefix, state_version",
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::Expired),
            fail_reason,
            upload_id,
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::InProgress)
        );

        if (result.empty()) {
            co_return std::nullopt;
        }

        co_return ExpiredUploadTransitionRecord{
            .cleanup = ToUploadTaskCleanupRecord(result[0]),
            .state_version = result[0]["state_version"].as<uint64_t>(),
        };
    }

    auto UploadTaskRepository::RecordChunkIfInProgress(
        const std::string& upload_id,
        uint64_t user_id,
        uint32_t chunk_index,
        uint64_t size_bytes,
        const std::string& hash_md5,
        const std::string& object_key,
        const std::string& etag
    ) const -> drogon::Task<ChunkRecordDisposition> {
        auto result = co_await m_db_client->execSqlCoro(
            "WITH eligible_task AS MATERIALIZED (" "  SELECT id FROM upload_tasks " "  WHERE id = $1 AND user_id = $2 AND status = $3 AND expires_at >= NOW() " "  FOR UPDATE" "), persisted_chunk AS (" "  INSERT INTO upload_task_chunks " "    (task_id, chunk_index, size_bytes, hash_md5, object_key, etag, uploaded_at) " "  SELECT id, $4, $5, $6, NULLIF($7::text, ''), NULLIF($8::text, ''), NOW() " "  FROM eligible_task " "  ON CONFLICT (task_id, chunk_index) DO UPDATE SET " "    size_bytes = COALESCE(upload_task_chunks.size_bytes, EXCLUDED.size_bytes), " "    hash_md5 = COALESCE(upload_task_chunks.hash_md5, EXCLUDED.hash_md5), " "    object_key = COALESCE(upload_task_chunks.object_key, EXCLUDED.object_key), " "    etag = COALESCE(upload_task_chunks.etag, EXCLUDED.etag) " "  WHERE (upload_task_chunks.size_bytes IS NULL " "         OR upload_task_chunks.size_bytes = EXCLUDED.size_bytes) " "    AND (upload_task_chunks.hash_md5 IS NULL " "         OR upload_task_chunks.hash_md5 = EXCLUDED.hash_md5) " "    AND (upload_task_chunks.object_key IS NULL " "         OR upload_task_chunks.object_key = EXCLUDED.object_key) " "    AND (upload_task_chunks.etag IS NULL OR EXCLUDED.etag IS NULL " "         OR upload_task_chunks.etag = EXCLUDED.etag) " "  RETURNING 1" ") " "SELECT EXISTS(SELECT 1 FROM eligible_task) AS task_eligible, " "       EXISTS(SELECT 1 FROM persisted_chunk) AS chunk_compatible",
            upload_id,
            user_id,
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::InProgress),
            static_cast<int32_t>(chunk_index),
            static_cast<int64_t>(size_bytes),
            hash_md5,
            object_key,
            etag
        );
        if (result.empty() || !result[0]["task_eligible"].as<bool>()) {
            co_return ChunkRecordDisposition::TaskRejected;
        }
        if (!result[0]["chunk_compatible"].as<bool>()) {
            co_return ChunkRecordDisposition::MetadataConflict;
        }
        co_return ChunkRecordDisposition::Accepted;
    }

    auto UploadTaskRepository::ListChunksForAssembly(const std::string& upload_id) const
        -> drogon::Task<std::vector<disk::storage::UploadStagingChunk>> {
        auto result = co_await m_db_client->execSqlCoro(
            "SELECT chunk_index, size_bytes, hash_md5, object_key, etag " "FROM upload_task_chunks WHERE task_id = $1 ORDER BY chunk_index",
            upload_id
        );

        std::vector<disk::storage::UploadStagingChunk> chunks;
        chunks.reserve(result.size());
        for (const auto& row : result) {
            chunks.push_back(disk::storage::UploadStagingChunk{
                .chunk_index = row["chunk_index"].as<uint32_t>(),
                .size_bytes = row["size_bytes"].isNull() ? 0 : row["size_bytes"].as<uint64_t>(),
                .md5_hash = row["hash_md5"].isNull() ? "" : row["hash_md5"].as<std::string>(),
                .object_key = row["object_key"].isNull() ? "" : row["object_key"].as<std::string>(),
                .etag = row["etag"].isNull() ? "" : row["etag"].as<std::string>(),
            });
        }
        co_return chunks;
    }

    auto UploadTaskRepository::ListUploadedChunkIndices(const std::string& upload_id) const
        -> drogon::Task<std::vector<uint32_t>> {
        auto result = co_await m_db_client->execSqlCoro(
            "SELECT chunk_index FROM upload_task_chunks WHERE task_id = $1 ORDER BY chunk_index",
            upload_id
        );

        std::vector<uint32_t> chunk_indices;
        chunk_indices.reserve(result.size());
        for (const auto& row : result) {
            chunk_indices.push_back(row["chunk_index"].as<uint32_t>());
        }

        co_return chunk_indices;
    }

    auto UploadTaskRepository::DeleteChunks(
        const drogon::orm::DbClientPtr& client,
        const std::string& upload_id
    ) const -> drogon::Task<void> {
        co_await client->execSqlCoro(
            "DELETE FROM upload_task_chunks WHERE task_id = $1",
            upload_id
        );
    }

    auto UploadTaskRepository::FindExpiredInProgressBatch(size_t limit) const
        -> drogon::Task<std::vector<ExpiredUploadTaskRecord>> {
        auto result = co_await m_db_client->execSqlCoro(
            "SELECT id, temp_path, user_id, reserved_bytes, staging_backend, " "COALESCE(staging_prefix, temp_path) AS staging_prefix FROM upload_tasks " "WHERE status = $1 AND expires_at < NOW() " "ORDER BY expires_at, id LIMIT $2",
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::InProgress),
            static_cast<int64_t>(limit)
        );

        std::vector<ExpiredUploadTaskRecord> records;
        records.reserve(result.size());
        for (const auto& row : result) {
            records.push_back(ToUploadTaskCleanupRecord(row));
        }

        co_return records;
    }

} // namespace disk::file
