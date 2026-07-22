/**
 * @file FolderRepository.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件夹持久化原语
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <drogon/orm/DbClient.h>
#include <trantor/utils/Date.h>

#include "dtos/FolderDto.hpp"
#include "models/Folders.hpp"
#include "services/FileServiceUtils.hpp"
#include "utils/LogHelper.hpp"

namespace disk::folder {

    using disk::file::utils::FolderDeletePlan;
    using disk::file::utils::FolderLocation;

    /**
     * @brief 文件夹持久化原语
     *
     * @details
     * 仅封装文件夹归属查询、递归子树查询和路径/计数更新等数据访问操作。
     * 不承载业务决策、事务创建或缓存失效。
     */
    class FolderRepository {
    public:
        explicit FolderRepository(drogon::orm::DbClientPtr db_client);

        [[nodiscard]]
        auto FindOwnedFolder(
            const drogon::orm::DbClientPtr& client,
            uint64_t folder_id,
            uint64_t user_id
        ) const -> drogon::Task<std::optional<drogon_model::disk::Folders>>;

        [[nodiscard]]
        auto ResolveOwnedFolderLocation(
            const drogon::orm::DbClientPtr& client,
            uint64_t folder_id,
            uint64_t user_id,
            disk::utils::LogContext log_context = {}
        ) const -> drogon::Task<Result<FolderLocation>>;

        [[nodiscard]]
        auto FetchFolderSubtree(
            const drogon::orm::DbClientPtr& client,
            uint64_t root_folder_id,
            uint64_t user_id
        ) const -> drogon::Task<std::vector<drogon_model::disk::Folders>>;

        [[nodiscard]]
        auto FetchFolderDeletePlan(
            const drogon::orm::DbClientPtr& client,
            uint64_t folder_id,
            uint64_t user_id
        ) const -> drogon::Task<std::optional<FolderDeletePlan>>;

        [[nodiscard]]
        auto FetchBatchFolderDeletePlans(
            const drogon::orm::DbClientPtr& client,
            const std::vector<uint64_t>& folder_ids,
            uint64_t user_id
        ) const -> drogon::Task<std::unordered_map<uint64_t, FolderDeletePlan>>;

        [[nodiscard]]
        auto FetchFolderTreeRows(
            const drogon::orm::DbClientPtr& client,
            uint64_t user_id,
            uint64_t parent_id,
            int max_depth
        ) const -> drogon::Task<std::vector<FolderNodeData>>;

        [[nodiscard]]
        auto FetchBreadcrumbRows(
            const drogon::orm::DbClientPtr& client,
            uint64_t folder_id,
            uint64_t user_id
        ) const -> drogon::Task<std::vector<BreadcrumbItem>>;

        [[nodiscard]]
        auto RenameFolderPath(
            const drogon::orm::DbClientPtr& client,
            uint64_t folder_id,
            uint64_t user_id,
            const std::string& new_name,
            const std::string& new_path,
            const trantor::Date& updated_at
        ) const -> drogon::Task<bool>;

        [[nodiscard]]
        auto UpdateFolderPath(
            const drogon::orm::DbClientPtr& client,
            uint64_t folder_id,
            uint64_t user_id,
            const std::string& new_path,
            const trantor::Date& updated_at
        ) const -> drogon::Task<bool>;

        [[nodiscard]]
        auto UpdateFolderLocationForMove(
            const drogon::orm::DbClientPtr& client,
            uint64_t folder_id,
            uint64_t user_id,
            uint64_t parent_id,
            const std::string& new_path,
            int depth_delta,
            const trantor::Date& updated_at
        ) const -> drogon::Task<bool>;

        [[nodiscard]]
        auto UpdateFolderPathForMove(
            const drogon::orm::DbClientPtr& client,
            uint64_t folder_id,
            uint64_t user_id,
            const std::string& new_path,
            int depth_delta,
            const trantor::Date& updated_at
        ) const -> drogon::Task<bool>;

        auto ApplyItemCountDelta(
            const drogon::orm::DbClientPtr& client,
            uint64_t folder_id,
            uint64_t user_id,
            int delta,
            const trantor::Date& updated_at
        ) const -> drogon::Task<void>;

        auto IncrementItemCount(
            const drogon::orm::DbClientPtr& client,
            uint64_t folder_id
        ) const -> drogon::Task<void>;

    private:
        drogon::orm::DbClientPtr m_db_client;
    };

} ///< namespace disk::folder
