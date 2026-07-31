/**
 * @file TrashQuery.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 回收站查询边界
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

namespace disk::trash {

    struct TrashListRecord {
        uint64_t id{ 0 };
        std::string item_type;
        uint64_t item_id{ 0 };
        std::string item_name;
        uint64_t item_size{ 0 };
        std::string original_path;
        std::string deleted_at;
        std::string expires_at;
    };

    struct TrashLifecycleRecord {
        uint64_t id{ 0 };
        uint64_t user_id{ 0 };
        std::string item_type;
        uint64_t item_id{ 0 };
        std::string item_name;
        uint64_t item_size{ 0 };
        uint64_t original_folder_id{ 0 };
        std::string original_path;
        std::string item_data;
        std::optional<uint64_t> content_id;
    };

    /**
     * @brief 回收站查询对象
     *
     * @details
     * 仅封装回收站读路径和批量预取 SQL，不承载恢复、永久删除、
     * 配额释放、引用计数更新或 blob 清理等业务决策。
     */
    class TrashQuery {
    public:
        explicit TrashQuery(drogon::orm::DbClientPtr db_client);

        [[nodiscard]]
        auto FetchListPageForUser(uint64_t user_id, int limit, int offset) const
            -> drogon::Task<std::vector<TrashListRecord>>;

        [[nodiscard]]
        auto CountForUser(uint64_t user_id) const -> drogon::Task<int>;

        [[nodiscard]]
        auto PrefetchLifecycleRowsByIds(const std::vector<uint64_t>& trash_ids) const
            -> drogon::Task<std::vector<TrashLifecycleRecord>>;

        [[nodiscard]]
        auto FetchLifecycleRowForUpdate(
            const drogon::orm::DbClientPtr& client,
            uint64_t trash_id,
            uint64_t user_id
        ) const -> drogon::Task<std::optional<TrashLifecycleRecord>>;

        [[nodiscard]]
        auto FetchLifecycleRowsForUser(uint64_t user_id) const
            -> drogon::Task<std::vector<TrashLifecycleRecord>>;

        [[nodiscard]]
        auto FetchExpiredLifecycleBatchAfterId(uint64_t last_seen_id, int limit) const
            -> drogon::Task<std::vector<TrashLifecycleRecord>>;

    private:
        drogon::orm::DbClientPtr m_db_client;
    };

} ///< namespace disk::trash
