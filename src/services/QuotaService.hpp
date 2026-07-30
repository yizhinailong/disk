/**
 * @file QuotaService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 存储配额与容量核算领域服务
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>

#include <drogon/orm/DbClient.h>

#include "utils/ErrorCode.hpp"
#include "utils/LogHelper.hpp"

namespace disk::quota {

    /**
     * @brief 存储配额与容量核算领域服务
     *
     * 集中封装 users.storage_used 与 users.storage_reserved 的配额检查、预留、
     * 释放、预留转已用与已用空间调整。
     */
    class QuotaService {
    public:
        explicit QuotaService(drogon::orm::DbClientPtr db_client);
        ~QuotaService() = default;
        QuotaService(const QuotaService&) = delete;
        auto operator=(const QuotaService&) -> QuotaService& = delete;
        QuotaService(QuotaService&&) = default;
        auto operator=(QuotaService&&) -> QuotaService& = default;

        [[nodiscard]]
        auto ReserveStorage(
            uint64_t user_id,
            uint64_t bytes,
            disk::utils::LogContext log_context = {}
        ) const
            -> drogon::Task<Result<void>>;

        [[nodiscard]]
        auto ReserveStorage(
            const drogon::orm::DbClientPtr& client,
            uint64_t user_id,
            uint64_t bytes,
            disk::utils::LogContext log_context = {}
        ) const -> drogon::Task<Result<void>>;

        [[nodiscard]]
        auto ReserveUploadStorage(
            uint64_t user_id,
            uint64_t bytes,
            disk::utils::LogContext log_context = {}
        ) const
            -> drogon::Task<Result<void>>;

        [[nodiscard]]
        auto ReserveUploadStorage(
            const drogon::orm::DbClientPtr& client,
            uint64_t user_id,
            uint64_t bytes,
            disk::utils::LogContext log_context = {}
        ) const -> drogon::Task<Result<void>>;

        auto ReleaseReservedStorage(
            uint64_t user_id,
            uint64_t bytes,
            disk::utils::LogContext log_context = {}
        ) const
            -> drogon::Task<void>;

        auto ReleaseReservedStorage(
            const drogon::orm::DbClientPtr& client,
            uint64_t user_id,
            uint64_t bytes,
            disk::utils::LogContext log_context = {}
        ) const -> drogon::Task<void>;

        [[nodiscard]]
        auto ReleaseReservedStorageChecked(
            const drogon::orm::DbClientPtr& client,
            uint64_t user_id,
            uint64_t bytes,
            disk::utils::LogContext log_context = {}
        ) const -> drogon::Task<Result<void>>;

        [[nodiscard]]
        auto CommitReservedToUsed(
            const drogon::orm::DbClientPtr& client,
            uint64_t user_id,
            uint64_t bytes,
            disk::utils::LogContext log_context = {}
        ) const -> drogon::Task<Result<void>>;

        [[nodiscard]]
        auto ConsumeUsedStorage(
            const drogon::orm::DbClientPtr& client,
            uint64_t user_id,
            uint64_t bytes,
            disk::utils::LogContext log_context = {}
        ) const -> drogon::Task<Result<void>>;

        auto AdjustUsedStorage(
            const drogon::orm::DbClientPtr& client,
            uint64_t user_id,
            int64_t delta,
            disk::utils::LogContext log_context = {}
        ) const -> drogon::Task<void>;

        [[nodiscard]]
        auto AdjustUsedStorageChecked(
            const drogon::orm::DbClientPtr& client,
            uint64_t user_id,
            int64_t delta,
            disk::utils::LogContext log_context = {}
        ) const -> drogon::Task<Result<void>>;

    private:
        drogon::orm::DbClientPtr m_db_client;
    };

} // namespace disk::quota
