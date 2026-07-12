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
#include <optional>

#include <drogon/orm/DbClient.h>

#include "utils/ErrorCode.hpp"

namespace disk::quota {

    struct AccountingReconciliation {
        uint64_t user_id{ 0 };
        uint64_t storage_used{ 0 };
        uint64_t storage_reserved{ 0 };
        uint64_t storage_quota{ 0 };
        uint64_t active_file_bytes{ 0 };
        uint64_t trash_item_bytes{ 0 };
        uint64_t in_progress_reserved_bytes{ 0 };
    };

    /**
     * @brief 存储配额与容量核算领域服务
     *
     * 集中封装 users.storage_used 与 users.storage_reserved 的配额检查、预留、
     * 释放、预留转已用、已用空间调整，以及只读诊断对账查询。
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
        auto ReserveUploadStorage(uint64_t user_id, uint64_t bytes) const
            -> drogon::Task<Result<void>>;

        [[nodiscard]]
        auto ReserveUploadStorage(
            const drogon::orm::DbClientPtr& client,
            uint64_t user_id,
            uint64_t bytes
        ) const -> drogon::Task<Result<void>>;

        auto ReleaseReservedStorage(uint64_t user_id, uint64_t bytes) const
            -> drogon::Task<void>;

        auto ReleaseReservedStorage(
            const drogon::orm::DbClientPtr& client,
            uint64_t user_id,
            uint64_t bytes
        ) const -> drogon::Task<void>;

        [[nodiscard]]
        auto ReleaseReservedStorageChecked(
            const drogon::orm::DbClientPtr& client,
            uint64_t user_id,
            uint64_t bytes
        ) const -> drogon::Task<Result<void>>;

        [[nodiscard]]
        auto CommitReservedToUsed(
            const drogon::orm::DbClientPtr& client,
            uint64_t user_id,
            uint64_t bytes
        ) const -> drogon::Task<Result<void>>;

        [[nodiscard]]
        auto ConsumeUsedStorage(
            const drogon::orm::DbClientPtr& client,
            uint64_t user_id,
            uint64_t bytes
        ) const -> drogon::Task<Result<void>>;

        auto AdjustUsedStorage(
            const drogon::orm::DbClientPtr& client,
            uint64_t user_id,
            int64_t delta
        ) const -> drogon::Task<void>;

        [[nodiscard]]
        auto GetReconciliation(uint64_t user_id) const
            -> drogon::Task<std::optional<AccountingReconciliation>>;

        [[nodiscard]]
        auto GetReconciliation(
            const drogon::orm::DbClientPtr& client,
            uint64_t user_id
        ) const -> drogon::Task<std::optional<AccountingReconciliation>>;

    private:
        drogon::orm::DbClientPtr m_db_client;
    };

} ///< namespace disk::quota
