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
        Logger::Debug() << "QuotaService initialization completed";
    }

    auto QuotaService::ReserveUploadStorage(uint64_t user_id, uint64_t bytes) const
        -> drogon::Task<Result<void>> {
        co_return co_await ReserveUploadStorage(m_db_client, user_id, bytes);
    }

    auto QuotaService::ReserveUploadStorage(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        uint64_t bytes
    ) const -> drogon::Task<Result<void>> {
        if (bytes == 0) {
            co_return {};
        }

        try {
            auto result = co_await client->execSqlCoro(
                "UPDATE users SET storage_reserved = storage_reserved + $1 "
                "WHERE id = $2 AND storage_used + storage_reserved + $3 <= storage_quota",
                bytes,
                user_id,
                bytes
            );

            if (result.affectedRows() == 0) {
                Logger::Warn() << "Insufficient storage quota for reservation: user_id=" << user_id
                         << ", bytes=" << bytes;
                co_return std::unexpected(ErrorInfo(ErrorCode::StorageQuotaExceeded));
            }

            Logger::Debug() << "Storage quota reserved: user_id=" << user_id
                      << ", bytes=" << bytes;
            co_return {};
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to reserve storage quota: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to reserve storage quota")
            );
        }
    }

    auto QuotaService::ReleaseReservedStorage(uint64_t user_id, uint64_t bytes) const
        -> drogon::Task<void> {
        co_await ReleaseReservedStorage(m_db_client, user_id, bytes);
    }

    auto QuotaService::ReleaseReservedStorage(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        uint64_t bytes
    ) const -> drogon::Task<void> {
        if (bytes == 0) {
            co_return;
        }

        try {
            co_await client->execSqlCoro(
                "UPDATE users SET storage_reserved = GREATEST(storage_reserved - $1, 0) WHERE id = $2",
                bytes,
                user_id
            );

            Logger::Debug() << "Reserved quota released: user_id=" << user_id
                      << ", bytes=" << bytes;
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to release reserved quota: " << e.base().what();
        }
    }

    auto QuotaService::CommitReservedToUsed(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        uint64_t bytes
    ) const -> drogon::Task<Result<void>> {
        if (bytes == 0) {
            co_return {};
        }

        try {
            auto result = co_await client->execSqlCoro(
                "UPDATE users SET storage_reserved = GREATEST(storage_reserved - $1, 0), "
                "storage_used = storage_used + $2 WHERE id = $3",
                bytes,
                bytes,
                user_id
            );

            if (result.affectedRows() == 0) {
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::InternalError,
                    "Failed to transfer reserved quota to used"
                ));
            }

            Logger::Debug() << "Reserved quota committed to used: user_id=" << user_id
                      << ", bytes=" << bytes;
            co_return {};
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to commit reserved storage to used: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to transfer reserved quota to used")
            );
        }
    }

    auto QuotaService::ConsumeUsedStorage(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        uint64_t bytes
    ) const -> drogon::Task<Result<void>> {
        if (bytes == 0) {
            co_return {};
        }

        try {
            auto result = co_await client->execSqlCoro(
                "UPDATE users SET storage_used = storage_used + $1 "
                "WHERE id = $2 AND storage_used + $3 <= storage_quota",
                bytes,
                user_id,
                bytes
            );

            if (result.affectedRows() == 0) {
                Logger::Warn() << "Insufficient storage space: user_id=" << user_id
                         << ", bytes=" << bytes;
                co_return std::unexpected(ErrorInfo(ErrorCode::StorageQuotaExceeded));
            }

            Logger::Debug() << "Storage quota check passed and consumed: user_id=" << user_id
                      << ", bytes=" << bytes;
            co_return {};
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to consume used storage quota: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to reserve storage quota")
            );
        }
    }

    auto QuotaService::AdjustUsedStorage(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        int64_t delta
    ) const -> drogon::Task<void> {
        if (delta == 0) {
            co_return;
        }

        try {
            if (delta > 0) {
                auto result = co_await client->execSqlCoro(
                    "UPDATE users SET storage_used = storage_used + $1 "
                    "WHERE id = $2 AND storage_used + $3 <= storage_quota",
                    static_cast<uint64_t>(delta),
                    user_id,
                    static_cast<uint64_t>(delta)
                );

                if (result.affectedRows() == 0) {
                    Logger::Warn() << "Skipped storage usage increment due to quota limit: user_id="
                             << user_id << ", delta=" << delta;
                    co_return;
                }
            } else {
                co_await client->execSqlCoro(
                    "UPDATE users SET storage_used = GREATEST(CAST(storage_used AS BIGINT) + $1, 0) "
                    "WHERE id = $2",
                    delta,
                    user_id
                );
            }

            Logger::Debug() << "Storage usage updated: user_id=" << user_id << ", delta=" << delta;
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to update storage usage: " << e.base().what();
        }
    }

    auto QuotaService::GetReconciliation(uint64_t user_id) const
        -> drogon::Task<std::optional<AccountingReconciliation>> {
        co_return co_await GetReconciliation(m_db_client, user_id);
    }

    auto QuotaService::GetReconciliation(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id
    ) const -> drogon::Task<std::optional<AccountingReconciliation>> {
        try {
            auto rows = co_await client->execSqlCoro(
                "SELECT u.id AS user_id, u.storage_used, u.storage_reserved, u.storage_quota, "
                "COALESCE((SELECT SUM(size) FROM files WHERE user_id = u.id), 0) AS active_file_bytes, "
                "COALESCE((SELECT SUM(item_size) FROM trash WHERE user_id = u.id), 0) AS trash_item_bytes, "
                "COALESCE((SELECT SUM(reserved_bytes) FROM upload_tasks "
                "          WHERE user_id = u.id AND status = 0), 0) AS in_progress_reserved_bytes "
                "FROM users u WHERE u.id = $1",
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
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "Failed to query storage accounting reconciliation: user_id="
                     << user_id << " - " << e.base().what();
            co_return std::nullopt;
        }
    }

} ///< namespace disk::quota
