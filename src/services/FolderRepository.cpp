/**
 * @file FolderRepository.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件夹持久化原语实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "FolderRepository.hpp"

#include <algorithm>
#include <utility>

#include "FileRepository.hpp"
#include "utils/BatchUtils.hpp"

namespace disk::folder {

    using disk::file::FileRepository;
    using disk::utils::BatchUtils;
    using drogon_model::disk::Folders;

    namespace {
        constexpr auto kSelectOwnedFolderSql =
            "SELECT id, user_id, parent_id, name, path, depth, item_count, created_at, updated_at "
            "FROM folders WHERE id = $1 AND user_id = $2";

        constexpr auto kSelectOwnedFolderForUpdateSql =
            "SELECT id, user_id, parent_id, name, path, depth, item_count, created_at, updated_at "
            "FROM folders WHERE id = $1 AND user_id = $2 FOR UPDATE";

        constexpr auto kAcquireNameLockSql =
            "SELECT pg_advisory_xact_lock(hashtextextended($1, 0))";

        constexpr auto kNameExistsExcludingSql =
            "SELECT 1 FROM folders "
            "WHERE user_id = $1 AND parent_id = $2 AND name = $3 AND id <> $4 LIMIT 1";

        constexpr auto kInsertFolderIfNameAvailableSql =
            "INSERT INTO folders ("
            "user_id, parent_id, name, path, depth, item_count, created_at, updated_at"
            ") VALUES ($1, $2, $3, $4, $5, $6, $7, $8) "
            "ON CONFLICT (user_id, parent_id, name) DO NOTHING "
            "RETURNING id, user_id, parent_id, name, path, depth, item_count, created_at, updated_at";

        constexpr auto kResolveOwnedFolderLocationSql =
            "SELECT path, depth FROM folders WHERE id = $1 AND user_id = $2";

        constexpr auto kFetchFolderSubtreeSql =
            "WITH RECURSIVE folder_tree AS ( "
            "SELECT id, user_id, parent_id, name, path, depth, item_count, created_at, updated_at "
            "FROM folders WHERE id = $1 AND user_id = $2 "
            "UNION ALL "
            "SELECT f.id, f.user_id, f.parent_id, f.name, f.path, f.depth, f.item_count, f.created_at, f.updated_at "
            "FROM folders f INNER JOIN folder_tree ft ON f.parent_id = ft.id "
            "WHERE f.user_id = $2 "
            ") SELECT id, user_id, parent_id, name, path, depth, item_count, created_at, updated_at "
            "FROM folder_tree ORDER BY depth ASC, id ASC";

        constexpr auto kFetchBatchFolderSubtreeSqlPrefix =
            "WITH RECURSIVE folder_tree AS ( "
            "SELECT id, user_id, parent_id, name, path, depth, item_count, created_at, updated_at, "
            "id AS root_id "
            "FROM folders WHERE id IN (";

        constexpr auto kFetchBatchFolderSubtreeSqlSuffix =
            ") AND user_id = $1 "
            "UNION ALL "
            "SELECT f.id, f.user_id, f.parent_id, f.name, f.path, f.depth, f.item_count, f.created_at, f.updated_at, "
            "ft.root_id "
            "FROM folders f INNER JOIN folder_tree ft ON f.parent_id = ft.id "
            "WHERE f.user_id = $1 "
            ") SELECT id, user_id, parent_id, name, path, depth, item_count, created_at, updated_at, root_id "
            "FROM folder_tree ORDER BY root_id ASC, depth ASC, id ASC";

        constexpr auto kFetchFolderTreeRowsSql =
            "WITH RECURSIVE folder_tree AS ("
            "SELECT id, name, parent_id, path, 0 AS level "
            "FROM folders "
            "WHERE user_id = $1 AND parent_id = $2 "
            "UNION ALL "
            "SELECT f.id, f.name, f.parent_id, f.path, ft.level + 1 "
            "FROM folders f "
            "INNER JOIN folder_tree ft ON f.parent_id = ft.id "
            "WHERE f.user_id = $3 AND ft.level < $4"
            ") "
            "SELECT id, name, parent_id FROM folder_tree ORDER BY path";

        constexpr auto kFetchBreadcrumbRowsSql =
            "WITH RECURSIVE ancestors AS ("
            "  SELECT id, name, parent_id FROM folders WHERE id = $1 AND user_id = $2"
            "  UNION ALL"
            "  SELECT f.id, f.name, f.parent_id FROM folders f"
            "  INNER JOIN ancestors a ON f.id = a.parent_id"
            "  WHERE f.user_id = $2"
            ") SELECT id, name FROM ancestors";

        constexpr auto kRenameFolderPathSql =
            "UPDATE folders SET name = $1, path = $2, updated_at = $3 "
            "WHERE id = $4 AND user_id = $5";

        constexpr auto kUpdateFolderPathSql =
            "UPDATE folders SET path = $1, updated_at = $2 WHERE id = $3 AND user_id = $4";

        constexpr auto kUpdateFolderLocationForMoveSql =
            "UPDATE folders SET parent_id = $1, path = $2, depth = depth + $3, updated_at = $4 "
            "WHERE id = $5 AND user_id = $6";

        constexpr auto kUpdateFolderPathForMoveSql =
            "UPDATE folders SET path = $1, depth = depth + $2, updated_at = $3 "
            "WHERE id = $4 AND user_id = $5";

        constexpr auto kApplyItemCountDeltaSql =
            "UPDATE folders SET item_count = GREATEST(item_count + $1, 0), updated_at = $2 "
            "WHERE id = $3 AND user_id = $4";
    } ///< namespace

    auto FolderRepository::FindOwnedFolder(
        const drogon::orm::DbClientPtr& client,
        uint64_t folder_id,
        uint64_t user_id
    ) const -> drogon::Task<std::optional<Folders>> {
        auto result = co_await client->execSqlCoro(kSelectOwnedFolderSql, folder_id, user_id);
        if (result.empty()) {
            co_return std::nullopt;
        }
        co_return Folders(result[0], -1);
    }

    auto FolderRepository::FindOwnedFolderForUpdate(
        const drogon::orm::DbClientPtr& client,
        uint64_t folder_id,
        uint64_t user_id
    ) const -> drogon::Task<std::optional<Folders>> {
        auto result = co_await client->execSqlCoro(
            kSelectOwnedFolderForUpdateSql,
            folder_id,
            user_id
        );
        if (result.empty()) {
            co_return std::nullopt;
        }
        co_return Folders(result[0], -1);
    }

    auto FolderRepository::AcquireNameLock(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        uint64_t parent_id,
        const std::string& name
    ) const -> drogon::Task<void> {
        const auto lock_key = "folder-name:" + std::to_string(user_id) + ":" +
                              std::to_string(parent_id) + ":" + name;
        co_await client->execSqlCoro(kAcquireNameLockSql, lock_key);
    }

    auto FolderRepository::NameExistsExcluding(
        const drogon::orm::DbClientPtr& client,
        const std::string& name,
        uint64_t parent_id,
        uint64_t user_id,
        uint64_t excluded_folder_id
    ) const -> drogon::Task<bool> {
        auto result = co_await client->execSqlCoro(
            kNameExistsExcludingSql,
            user_id,
            parent_id,
            name,
            excluded_folder_id
        );
        co_return !result.empty();
    }

    auto FolderRepository::InsertIfNameAvailable(
        const drogon::orm::DbClientPtr& client,
        Folders folder
    ) const -> drogon::Task<std::optional<Folders>> {
        auto result = co_await client->execSqlCoro(
            kInsertFolderIfNameAvailableSql,
            folder.getValueOfUserId(),
            folder.getValueOfParentId(),
            folder.getValueOfName(),
            folder.getValueOfPath(),
            folder.getValueOfDepth(),
            folder.getValueOfItemCount(),
            folder.getValueOfCreatedAt(),
            folder.getValueOfUpdatedAt()
        );
        if (result.empty()) {
            co_return std::nullopt;
        }
        co_return Folders(result[0], -1);
    }

    auto FolderRepository::ResolveOwnedFolderLocation(
        const drogon::orm::DbClientPtr& client,
        uint64_t folder_id,
        uint64_t user_id,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<Result<FolderLocation>> {
        if (folder_id == 0) {
            co_return FolderLocation{};
        }

        try {
            auto result = co_await client->execSqlCoro(
                kResolveOwnedFolderLocationSql,
                folder_id,
                user_id
            );
            if (result.empty()) {
                co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
            }

            co_return FolderLocation{ .path = result[0]["path"].as<std::string>(),
                                      .depth = result[0]["depth"].as<uint32_t>() };
        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Warn(log_context) << "Folder location lookup failed";
            co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
        }
    }

    auto FolderRepository::FetchFolderSubtree(
        const drogon::orm::DbClientPtr& client,
        uint64_t root_folder_id,
        uint64_t user_id
    ) const -> drogon::Task<std::vector<Folders>> {
        auto result = co_await client->execSqlCoro(kFetchFolderSubtreeSql, root_folder_id, user_id);
        std::vector<Folders> folders;
        folders.reserve(result.size());
        for (const auto& row : result) {
            folders.emplace_back(row, -1);
        }
        co_return folders;
    }

    auto FolderRepository::FetchBatchFolderDeletePlans(
        const drogon::orm::DbClientPtr& client,
        const std::vector<uint64_t>& folder_ids,
        uint64_t user_id
    ) const -> drogon::Task<std::unordered_map<uint64_t, FolderDeletePlan>> {
        std::unordered_map<uint64_t, FolderDeletePlan> plans;
        if (folder_ids.empty()) {
            co_return plans;
        }

        auto folder_result = co_await client->execSqlCoro(
            std::string(kFetchBatchFolderSubtreeSqlPrefix) +
                BatchUtils::BuildSafeNumericInClause(folder_ids) + kFetchBatchFolderSubtreeSqlSuffix,
            user_id
        );

        std::vector<uint64_t> all_folder_ids;
        for (const auto& row : folder_result) {
            auto root_id = row["root_id"].as<uint64_t>();
            auto folder_id_val = row["id"].as<uint64_t>();
            auto [plan_it, inserted] = plans.try_emplace(root_id);
            plan_it->second.folders.emplace_back(row, -1);
            if (inserted) {
                plan_it->second.root = plan_it->second.folders.front();
            }
            all_folder_ids.push_back(folder_id_val);
        }

        if (all_folder_ids.empty()) {
            co_return plans;
        }

        std::sort(all_folder_ids.begin(), all_folder_ids.end());
        all_folder_ids.erase(std::unique(all_folder_ids.begin(), all_folder_ids.end()), all_folder_ids.end());

        FileRepository file_repository;
        auto files = co_await file_repository.FetchFilesInFolders(client, all_folder_ids, user_id);

        std::unordered_map<uint64_t, std::vector<uint64_t>> root_to_folder_ids;
        for (const auto& [root_id, plan] : plans) {
            auto& ids = root_to_folder_ids[root_id];
            ids.reserve(plan.folders.size());
            for (const auto& folder : plan.folders) {
                ids.push_back(folder.getValueOfId());
            }
            std::sort(ids.begin(), ids.end());
        }

        for (const auto& file : files) {
            auto folder_id_val = file.getValueOfFolderId();
            uint64_t matched_root = 0;
            for (const auto& [root_id, folder_id_list] : root_to_folder_ids) {
                if (std::binary_search(folder_id_list.begin(), folder_id_list.end(), folder_id_val)) {
                    matched_root = root_id;
                    break;
                }
            }
            if (matched_root == 0) continue;

            auto plan_it = plans.find(matched_root);
            if (plan_it != plans.end()) {
                plan_it->second.item_size += file.getValueOfSize();
                plan_it->second.files.push_back(file);
            }
        }

        co_return plans;
    }

    auto FolderRepository::FetchFolderTreeRows(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        uint64_t parent_id,
        int max_depth
    ) const -> drogon::Task<std::vector<FolderNodeData>> {
        auto result = co_await client->execSqlCoro(
            kFetchFolderTreeRowsSql,
            user_id,
            parent_id,
            user_id,
            max_depth
        );

        std::vector<FolderNodeData> nodes;
        nodes.reserve(result.size());
        for (const auto& row : result) {
            nodes.push_back(FolderNodeData{
                .id = row["id"].as<uint64_t>(),
                .name = row["name"].as<std::string>(),
                .parent_id = row["parent_id"].as<uint64_t>(),
            });
        }

        co_return nodes;
    }

    auto FolderRepository::FetchBreadcrumbRows(
        const drogon::orm::DbClientPtr& client,
        uint64_t folder_id,
        uint64_t user_id
    ) const -> drogon::Task<std::vector<BreadcrumbItem>> {
        auto result = co_await client->execSqlCoro(kFetchBreadcrumbRowsSql, folder_id, user_id);
        std::vector<BreadcrumbItem> path;
        path.reserve(result.size());
        for (const auto& row : result) {
            path.push_back(BreadcrumbItem{
                .id = row["id"].as<uint64_t>(),
                .name = row["name"].as<std::string>()
            });
        }
        co_return path;
    }

    auto FolderRepository::RenameFolderPath(
        const drogon::orm::DbClientPtr& client,
        uint64_t folder_id,
        uint64_t user_id,
        const std::string& new_name,
        const std::string& new_path,
        const trantor::Date& updated_at
    ) const -> drogon::Task<bool> {
        auto result = co_await client->execSqlCoro(
            kRenameFolderPathSql,
            new_name,
            new_path,
            updated_at,
            folder_id,
            user_id
        );
        co_return result.affectedRows() > 0;
    }

    auto FolderRepository::UpdateFolderPath(
        const drogon::orm::DbClientPtr& client,
        uint64_t folder_id,
        uint64_t user_id,
        const std::string& new_path,
        const trantor::Date& updated_at
    ) const -> drogon::Task<bool> {
        auto result = co_await client->execSqlCoro(
            kUpdateFolderPathSql,
            new_path,
            updated_at,
            folder_id,
            user_id
        );
        co_return result.affectedRows() > 0;
    }

    auto FolderRepository::UpdateFolderLocationForMove(
        const drogon::orm::DbClientPtr& client,
        uint64_t folder_id,
        uint64_t user_id,
        uint64_t parent_id,
        const std::string& new_path,
        int depth_delta,
        const trantor::Date& updated_at
    ) const -> drogon::Task<bool> {
        auto result = co_await client->execSqlCoro(
            kUpdateFolderLocationForMoveSql,
            parent_id,
            new_path,
            depth_delta,
            updated_at,
            folder_id,
            user_id
        );
        co_return result.affectedRows() > 0;
    }

    auto FolderRepository::UpdateFolderPathForMove(
        const drogon::orm::DbClientPtr& client,
        uint64_t folder_id,
        uint64_t user_id,
        const std::string& new_path,
        int depth_delta,
        const trantor::Date& updated_at
    ) const -> drogon::Task<bool> {
        auto result = co_await client->execSqlCoro(
            kUpdateFolderPathForMoveSql,
            new_path,
            depth_delta,
            updated_at,
            folder_id,
            user_id
        );
        co_return result.affectedRows() > 0;
    }

    auto FolderRepository::ApplyItemCountDelta(
        const drogon::orm::DbClientPtr& client,
        uint64_t folder_id,
        uint64_t user_id,
        int delta,
        const trantor::Date& updated_at
    ) const -> drogon::Task<bool> {
        auto result = co_await client->execSqlCoro(
            kApplyItemCountDeltaSql,
            delta,
            updated_at,
            folder_id,
            user_id
        );
        co_return result.affectedRows() > 0;
    }

} ///< namespace disk::folder
