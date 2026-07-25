/**
 * @file QuotaService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 存储配额与容量核算领域服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "QuotaService.hpp"

#include <utility>

namespace disk::quota {

    QuotaService::QuotaService(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
        Logger::Debug(disk::utils::ServiceRuntimeLogContext()) << "Service initialized: service=quota";
    }

    auto QuotaService::ReserveStorage(
        uint64_t user_id,
        uint64_t bytes,
        disk::utils::LogContext log_context
    ) const
        -> drogon::Task<Result<void>> {
        co_return co_await ReserveStorage(m_db_client, user_id, bytes, log_context);
    }

    auto QuotaService::ReserveStorage(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        uint64_t bytes,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<Result<void>> {
        if (bytes == 0) {
            co_return {};
        }

        try {
            auto result = co_await client->execSqlCoro(
                "UPDATE users SET storage_reserved = storage_reserved + $1 " "WHERE id = $2 AND storage_used + storage_reserved + $3 <= storage_quota",
                bytes,
                user_id,
                bytes
            );

            if (result.affectedRows() == 0) {
                Logger::Warn(log_context) << "Storage quota reservation rejected";
                co_return std::unexpected(ErrorInfo(ErrorCode::StorageQuotaExceeded));
            }

            Logger::Debug(log_context) << "Storage quota reserved";
            co_return {};
        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Error(log_context) << "Storage quota reservation failed";
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to reserve storage quota")
            );
        }
    }

    auto QuotaService::ReserveUploadStorage(
        uint64_t user_id,
        uint64_t bytes,
        disk::utils::LogContext log_context
    ) const
        -> drogon::Task<Result<void>> {
        co_return co_await ReserveStorage(m_db_client, user_id, bytes, log_context);
    }

    auto QuotaService::ReserveUploadStorage(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        uint64_t bytes,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<Result<void>> {
        co_return co_await ReserveStorage(client, user_id, bytes, log_context);
    }

    auto QuotaService::ReleaseReservedStorage(
        uint64_t user_id,
        uint64_t bytes,
        disk::utils::LogContext log_context
    ) const
        -> drogon::Task<void> {
        co_await ReleaseReservedStorage(m_db_client, user_id, bytes, log_context);
    }

    auto QuotaService::ReleaseReservedStorage(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        uint64_t bytes,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<void> {
        auto result = co_await ReleaseReservedStorageChecked(
            client,
            user_id,
            bytes,
            log_context
        );
        if (!result) {
            Logger::Error(log_context) << "Reserved quota release failed";
        }
    }

    auto QuotaService::ReleaseReservedStorageChecked(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        uint64_t bytes,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<Result<void>> {
        if (bytes == 0) {
            co_return {};
        }

        try {
            auto result = co_await client->execSqlCoro(
                "UPDATE users SET storage_reserved = storage_reserved - $1 " "WHERE id = $2 AND storage_reserved >= $3",
                bytes,
                user_id,
                bytes
            );

            if (result.affectedRows() == 0) {
                Logger::Error(log_context) << "Reserved quota release invariant failed";
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::InternalError,
                    "Failed to release reserved quota"
                ));
            }

            Logger::Debug(log_context) << "Reserved quota released";
            co_return {};
        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Error(log_context) << "Reserved quota release database operation failed";
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to release reserved quota")
            );
        }
    }

    auto QuotaService::CommitReservedToUsed(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        uint64_t bytes,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<Result<void>> {
        if (bytes == 0) {
            co_return {};
        }

        try {
            auto result = co_await client->execSqlCoro(
                "UPDATE users SET storage_reserved = storage_reserved - $1, " "storage_used = storage_used + $2 " "WHERE id = $3 AND storage_reserved >= $4",
                bytes,
                bytes,
                user_id,
                bytes
            );

            if (result.affectedRows() == 0) {
                Logger::Error(log_context) << "Reserved-to-used quota invariant failed";
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::InternalError,
                    "Failed to transfer reserved quota to used"
                ));
            }

            Logger::Debug(log_context) << "Reserved quota committed to used";
            co_return {};
        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Error(log_context) << "Reserved-to-used quota transfer failed";
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to transfer reserved quota to used")
            );
        }
    }

    auto QuotaService::ConsumeUsedStorage(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        uint64_t bytes,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<Result<void>> {
        if (bytes == 0) {
            co_return {};
        }

        try {
            auto result = co_await client->execSqlCoro(
                "UPDATE users SET storage_used = storage_used + $1 " "WHERE id = $2 AND storage_used + $3 <= storage_quota",
                bytes,
                user_id,
                bytes
            );

            if (result.affectedRows() == 0) {
                Logger::Warn(log_context) << "Used storage quota consumption rejected";
                co_return std::unexpected(ErrorInfo(ErrorCode::StorageQuotaExceeded));
            }

            Logger::Debug(log_context) << "Used storage quota consumed";
            co_return {};
        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Error(log_context) << "Used storage quota consumption failed";
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to reserve storage quota")
            );
        }
    }

    auto QuotaService::AdjustUsedStorage(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        int64_t delta,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<void> {
        auto result = co_await AdjustUsedStorageChecked(client, user_id, delta, log_context);
        if (!result) {
            Logger::Error(log_context) << "Storage usage adjustment failed";
        }
    }

    auto QuotaService::AdjustUsedStorageChecked(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        int64_t delta,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<Result<void>> {
        if (delta == 0) {
            co_return {};
        }

        try {
            if (delta > 0) {
                auto bytes = static_cast<uint64_t>(delta);
                auto result = co_await client->execSqlCoro(
                    "UPDATE users SET storage_used = storage_used + $1 " "WHERE id = $2 AND storage_used + $3 <= storage_quota",
                    bytes,
                    user_id,
                    bytes
                );

                if (result.affectedRows() == 0) {
                    Logger::Warn(log_context) << "Storage usage increment rejected";
                    co_return std::unexpected(ErrorInfo(ErrorCode::StorageQuotaExceeded));
                }
            } else {
                auto result = co_await client->execSqlCoro(
                    "UPDATE users SET storage_used = GREATEST(CAST(storage_used AS BIGINT) + $1, 0) " "WHERE id = $2",
                    delta,
                    user_id
                );

                if (result.affectedRows() == 0) {
                    co_return std::unexpected(ErrorInfo(
                        ErrorCode::InternalError,
                        "Failed to update storage usage"
                    ));
                }
            }

            Logger::Debug(log_context) << "Storage usage adjusted";
            co_return {};
        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Error(log_context) << "Storage usage adjustment database operation failed";
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to update storage usage")
            );
        }
    }

    auto QuotaService::GetReconciliation(
        uint64_t user_id,
        disk::utils::LogContext log_context
    ) const
        -> drogon::Task<std::optional<AccountingReconciliation>> {
        co_return co_await GetReconciliation(m_db_client, user_id, log_context);
    }

    auto QuotaService::GetReconciliation(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<std::optional<AccountingReconciliation>> {
        try {
            auto rows = co_await client->execSqlCoro(
                "SELECT u.id AS user_id, u.storage_used, u.storage_reserved, u.storage_quota, " "COALESCE((SELECT SUM(size) FROM files WHERE user_id = u.id), 0) AS active_file_bytes, " "COALESCE((SELECT SUM(item_size) FROM trash WHERE user_id = u.id), 0) AS trash_item_bytes, " "COALESCE((SELECT SUM(reserved_bytes) FROM upload_tasks " "          WHERE user_id = u.id AND status IN (0, 4)), 0) AS in_progress_reserved_bytes " "FROM users u WHERE u.id = $1",
                user_id
            );

            if (rows.empty()) {
                co_return std::nullopt;
            }

            AccountingReconciliation reconciliation;
            reconciliation.user_id = rows[0]["user_id"].as<uint64_t>();
            reconciliation.storage_used = rows[0]["storage_used"].as<uint64_t>();
            reconciliation.storage_reserved = rows[0]["storage_reserved"].as<uint64_t>();
            reconciliation.storage_quota = rows[0]["storage_quota"].as<uint64_t>();
            reconciliation.active_file_bytes = rows[0]["active_file_bytes"].as<uint64_t>();
            reconciliation.trash_item_bytes = rows[0]["trash_item_bytes"].as<uint64_t>();
            reconciliation.in_progress_reserved_bytes = rows[0]["in_progress_reserved_bytes"].as<uint64_t>();
            co_return reconciliation;
        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Warn(log_context) << "Storage accounting reconciliation query failed";
            co_return std::nullopt;
        }
    }

} // namespace disk::quota
