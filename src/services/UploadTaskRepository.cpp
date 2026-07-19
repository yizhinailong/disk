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

#include "FileServiceUtils.hpp"
#include "utils/BatchUtils.hpp"

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
    } // namespace

    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::UploadTasks;

    UploadTaskRepository::UploadTaskRepository(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
    }

    auto UploadTaskRepository::FindById(const std::string& upload_id) const
        -> drogon::Task<std::optional<UploadTasks>> {
        try {
            CoroMapper<UploadTasks> mapper(m_db_client);
            co_return co_await mapper.findByPrimaryKey(upload_id);
        } catch (const drogon::orm::DrogonDbException&) {
            co_return std::nullopt;
        }
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
            "SELECT id FROM upload_tasks "
            "WHERE user_id = $1 AND file_hash = $2 AND status = $3 LIMIT 1",
            user_id,
            file_hash,
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::InProgress)
        );

        if (result.empty()) {
            co_return std::nullopt;
        }

        co_return result[0]["id"].as<std::string>();
    }

    auto UploadTaskRepository::Create(UploadTasks task) const -> drogon::Task<UploadTasks> {
        auto result = co_await m_db_client->execSqlCoro(
            "INSERT INTO upload_tasks ("
            "id, user_id, folder_id, filename, file_size, file_hash, chunk_size, total_chunks, "
            "reserved_bytes, temp_path, status, expires_at"
            ") VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12) RETURNING *",
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
            static_cast<int16_t>(task.getValueOfStatus()),
            task.getValueOfExpiresAt()
        );

        co_return UploadTasks(result[0], -1);
    }

    auto UploadTaskRepository::ClaimFinalizeLease(
        const std::string& upload_id,
        uint64_t user_id,
        const std::string& lease_owner,
        uint32_t lease_duration_seconds
    ) const -> drogon::Task<FinalizeClaimResult> {
        ValidateLeaseArguments(lease_owner, lease_duration_seconds);

        auto claimed = co_await m_db_client->execSqlCoro(
            "UPDATE upload_tasks AS task SET "
            "status = $1, lease_owner = $2, "
            "lease_expires_at = NOW() + ($3::integer * INTERVAL '1 second'), "
            "state_version = state_version + 1, "
            "finalize_attempts = finalize_attempts + 1, "
            "last_error_code = NULL, last_error_at = NULL "
            "WHERE task.id = $4 AND task.user_id = $5 AND ("
            "  (task.status = $6 AND task.expires_at >= NOW() AND ("
            "    SELECT COUNT(*) = task.total_chunks "
            "       AND COALESCE(MAX(chunk.chunk_index), -1) = task.total_chunks - 1 "
            "    FROM upload_task_chunks AS chunk WHERE chunk.task_id = task.id"
            "  )) "
            "  OR (task.status = $1 AND task.lease_expires_at <= NOW())"
            ") "
            "RETURNING state_version, finalize_attempts",
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
            "SELECT status, state_version, finalize_attempts, completed_file_id, "
            "COALESCE(lease_expires_at <= NOW(), FALSE) AS lease_expired, "
            "expires_at < NOW() AS task_expired "
            "FROM upload_tasks WHERE id = $1 AND user_id = $2",
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
        const std::string& upload_id,
        uint64_t user_id,
        const std::string& lease_owner,
        uint64_t expected_state_version,
        uint32_t lease_duration_seconds
    ) const -> drogon::Task<std::optional<uint64_t>> {
        ValidateLeaseArguments(lease_owner, lease_duration_seconds);

        auto result = co_await m_db_client->execSqlCoro(
            "UPDATE upload_tasks SET "
            "lease_expires_at = NOW() + ($1::integer * INTERVAL '1 second'), "
            "state_version = state_version + 1 "
            "WHERE id = $2 AND user_id = $3 AND status = $4 "
            "AND lease_owner = $5 AND state_version = $6 "
            "AND lease_expires_at > NOW() "
            "RETURNING state_version",
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
            "UPDATE upload_tasks SET status = $1, completed_file_id = $2, "
            "finalized_at = NOW(), lease_owner = NULL, lease_expires_at = NULL, "
            "state_version = state_version + 1 "
            "WHERE id = $3 AND user_id = $4 AND status = $5 "
            "AND lease_owner = $6 AND state_version = $7 "
            "AND lease_expires_at > NOW()",
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

    auto UploadTaskRepository::MarkFailedIfLeaseOwned(
        const drogon::orm::DbClientPtr& client,
        const std::string& upload_id,
        uint64_t user_id,
        const std::string& lease_owner,
        uint64_t expected_state_version,
        int error_code,
        const std::string& fail_reason
    ) const -> drogon::Task<bool> {
        auto result = co_await client->execSqlCoro(
            "UPDATE upload_tasks SET status = $1, finalized_at = NOW(), "
            "lease_owner = NULL, lease_expires_at = NULL, "
            "state_version = state_version + 1, last_error_code = $2, "
            "last_error_at = NOW(), fail_reason = $3 "
            "WHERE id = $4 AND user_id = $5 AND status = $6 "
            "AND lease_owner = $7 AND state_version = $8 "
            "AND lease_expires_at > NOW()",
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::Failed),
            error_code,
            fail_reason,
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
            "UPDATE upload_tasks SET last_error_code = $1, last_error_at = NOW() "
            "WHERE id = $2 AND user_id = $3 AND status = $4 "
            "AND lease_owner = $5 AND state_version = $6 "
            "AND lease_expires_at > NOW()",
            error_code,
            upload_id,
            user_id,
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::Finalizing),
            lease_owner,
            expected_state_version
        );
        co_return result.affectedRows() > 0;
    }

    auto UploadTaskRepository::DeleteInProgressById(const std::string& upload_id) const
        -> drogon::Task<bool> {
        auto result = co_await m_db_client->execSqlCoro(
            "DELETE FROM upload_tasks WHERE id = $1 AND status = $2",
            upload_id,
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::InProgress)
        );
        co_return result.affectedRows() > 0;
    }

    auto UploadTaskRepository::MarkCompleted(std::string const& upload_id) const -> drogon::Task<bool> {
        co_return co_await MarkCompletedIfInProgress(m_db_client, upload_id);
    }

    auto UploadTaskRepository::MarkCompletedIfInProgress(
        const drogon::orm::DbClientPtr& client,
        const std::string& upload_id
    ) const -> drogon::Task<bool> {
        auto result = co_await client->execSqlCoro(
            "UPDATE upload_tasks SET status = $1, finalized_at = NOW() WHERE id = $2 AND status = $3",
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::Completed),
            upload_id,
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::InProgress)
        );
        co_return result.affectedRows() > 0;
    }

    auto UploadTaskRepository::MarkCancelledIfInProgress(
        const std::string& upload_id,
        const std::string& fail_reason
    ) const -> drogon::Task<bool> {
        auto result = co_await m_db_client->execSqlCoro(
            "UPDATE upload_tasks SET status = $1, finalized_at = NOW(), fail_reason = $2 "
            "WHERE id = $3 AND status = $4",
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::Cancelled),
            fail_reason,
            upload_id,
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::InProgress)
        );
        co_return result.affectedRows() > 0;
    }

    auto UploadTaskRepository::MarkExpiredIfInProgressBatch(
        const std::vector<std::string>& upload_ids,
        const std::string& fail_reason
    ) const -> drogon::Task<uint64_t> {
        if (upload_ids.empty()) {
            co_return 0;
        }

        auto placeholders = disk::utils::BatchUtils::BuildInPlaceholders(upload_ids);
        auto expired_status_param = static_cast<int>(upload_ids.size()) + 1;
        auto fail_reason_param = static_cast<int>(upload_ids.size()) + 2;
        auto in_progress_status_param = static_cast<int>(upload_ids.size()) + 3;
        auto result = co_await disk::file::utils::ExecSqlWithBindings(
            m_db_client,
            "UPDATE upload_tasks SET status = $" + std::to_string(expired_status_param) +
            ", finalized_at = NOW(), fail_reason = $" + std::to_string(fail_reason_param) +
            " WHERE id IN (" + placeholders + ") AND status = $" +
            std::to_string(in_progress_status_param),
            [&](auto& binder) {
                for (const auto& upload_id : upload_ids) {
                    binder << upload_id;
                }
                binder << disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::Expired)
                       << fail_reason
                       << disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::InProgress);
            }
        );

        co_return static_cast<uint64_t>(result.affectedRows());
    }

    auto UploadTaskRepository::MarkExpiredIfInProgressReturning(
        const drogon::orm::DbClientPtr& client,
        const std::string& upload_id,
        const std::string& fail_reason
    ) const -> drogon::Task<std::optional<ExpiredUploadTaskRecord>> {
        auto result = co_await client->execSqlCoro(
            "UPDATE upload_tasks SET status = $1, finalized_at = NOW(), fail_reason = $2 "
            "WHERE id = $3 AND status = $4 AND expires_at < NOW() "
            "RETURNING id, temp_path, user_id, reserved_bytes",
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::Expired),
            fail_reason,
            upload_id,
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::InProgress)
        );

        if (result.empty()) {
            co_return std::nullopt;
        }

        const auto& row = result[0];
        co_return ExpiredUploadTaskRecord{
            .id = row["id"].as<std::string>(),
            .temp_path = row["temp_path"].as<std::string>(),
            .user_id = row["user_id"].as<uint64_t>(),
            .reserved_bytes = row["reserved_bytes"].as<uint64_t>(),
        };
    }

    auto UploadTaskRepository::RecordChunkUploadedIfAbsent(
        const std::string& upload_id,
        uint32_t chunk_index
    ) const -> drogon::Task<bool> {
        auto result = co_await m_db_client->execSqlCoro(
            "INSERT INTO upload_task_chunks (task_id, chunk_index, uploaded_at) "
            "VALUES ($1, $2, NOW()) ON CONFLICT DO NOTHING",
            upload_id,
            chunk_index
        );
        co_return result.affectedRows() > 0;
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

    auto UploadTaskRepository::GetChunkCoverage(const std::string& upload_id) const
        -> drogon::Task<disk::upload::ChunkCoverage> {
        auto result = co_await m_db_client->execSqlCoro(
            "SELECT COUNT(*) AS uploaded_count, "
            "COALESCE(MAX(chunk_index), -1) AS max_chunk_index "
            "FROM upload_task_chunks WHERE task_id = $1",
            upload_id
        );

        if (result.empty()) {
            co_return disk::upload::ChunkCoverage{};
        }

        co_return disk::upload::ChunkCoverage{
            .uploaded_count = result[0]["uploaded_count"].as<uint64_t>(),
            .max_chunk_index = result[0]["max_chunk_index"].as<int64_t>(),
        };
    }

    auto UploadTaskRepository::DeleteChunks(std::string const& upload_id) const -> drogon::Task<void> {
        co_await DeleteChunks(m_db_client, upload_id);
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
            "SELECT id, temp_path, user_id, reserved_bytes FROM upload_tasks "
            "WHERE status = $1 AND expires_at < NOW() "
            "LIMIT $2",
            disk::upload::ToStorageValue(disk::upload::UploadTaskStatus::InProgress),
            static_cast<int64_t>(limit)
        );

        std::vector<ExpiredUploadTaskRecord> records;
        records.reserve(result.size());
        for (const auto& row : result) {
            records.push_back(ExpiredUploadTaskRecord{
                .id = row["id"].as<std::string>(),
                .temp_path = row["temp_path"].as<std::string>(),
                .user_id = row["user_id"].as<uint64_t>(),
                .reserved_bytes = row["reserved_bytes"].as<uint64_t>(),
            });
        }

        co_return records;
    }

} ///< namespace disk::file
