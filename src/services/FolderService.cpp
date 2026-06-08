/**
 * @file FolderService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件夹服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "FolderService.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "utils/BatchUtils.hpp"

namespace disk::folder {

    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::Folders;

    FolderService::FolderService(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
        Logger::Debug() << "FolderService initialization completed";
    }

    namespace {
        [[nodiscard]] auto BuildFolderPath(const std::string& parent_path, const std::string& name)
            -> std::string {
            return parent_path == "/" ? "/" + name + "/" : parent_path + name + "/";
        }

        [[nodiscard]] auto BuildFilePath(const std::string& folder_path, const std::string& filename)
            -> std::string {
            return folder_path == "/" ? "/" + filename : folder_path + filename;
        }
    }

    auto FolderService::CreateFolder(CreateFolderRequest request, uint64_t user_id)
        -> drogon::Task<Result<CreateFolderResponse>> {

        Logger::Debug() << "Starting create folder: name=\"" << request.name
                  << "\", parent_id=" << request.parent_id << ", user_id=" << user_id;

        /// 1. 验证父文件夹（如果 parent_id > 0）
        std::string parent_path = "/";
        uint32_t parent_depth = 0;

        if (request.parent_id > 0) {
            auto parent_result = co_await FindAndValidateParent(request.parent_id, user_id);
            if (!parent_result) {
                Logger::Warn() << "Parent folder validation failed: parent_id=" << request.parent_id;
                co_return std::unexpected(parent_result.error());
            }

            const auto& parent = *parent_result;
            parent_path = parent.getValueOfPath();
            parent_depth = parent.getValueOfDepth();
            Logger::Debug() << "Parent folder validated: path=" << parent_path
                      << ", depth=" << parent_depth;
        }

        /// 2. 检查同名文件夹是否已存在
        if (co_await IsFolderNameExists(request.name, request.parent_id, user_id)) {
            Logger::Warn() << "Folder with same name already exists: name=\"" << request.name
                     << "\", parent_id=" << request.parent_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::FolderAlreadyExists));
        }

        /// 3. 计算路径和深度
        std::string folder_path = parent_path + request.name + "/";
        uint32_t folder_depth = parent_depth + 1;

        Logger::Debug() << "Calculated folder path: path=\"" << folder_path
                  << "\", depth=" << folder_depth;

        /// 4. 创建文件夹记录
        Folders folder;
        folder.setUserId(user_id);
        folder.setParentId(request.parent_id);
        folder.setName(request.name);
        folder.setPath(folder_path);
        folder.setDepth(folder_depth);
        folder.setItemCount(0);
        folder.setCreatedAt(trantor::Date::now());
        folder.setUpdatedAt(trantor::Date::now());

        /// 5. 插入数据库
        try {
            CoroMapper<Folders> mapper(m_db_client);
            folder = co_await mapper.insert(folder);
            Logger::Info() << "Folder created successfully: name=\"" << request.name
                     << "\" (ID: " << folder.getValueOfId() << ", user_id: " << user_id << ")";
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Folder creation failed: name=\"" << request.name << "\" - "
                      << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to create folder, please try again later"
            ));
        }

        /// 6. 更新父文件夹的 item_count（如果 parent_id > 0）
        if (request.parent_id > 0) {
            co_await IncrementParentItemCount(request.parent_id);
        }

        /// 7. 构造响应
        CreateFolderResponse response;
        response.id = folder.getValueOfId();
        response.name = folder.getValueOfName();
        response.parent_id = folder.getValueOfParentId();
        response.path = folder.getValueOfPath();
        response.created_at = folder.getValueOfCreatedAt().toDbStringLocal();

        co_return response;
    }

    auto FolderService::Rename(uint64_t folder_id, std::string new_name, uint64_t user_id)
        -> drogon::Task<Result<RenameFolderResponse>> {

        Logger::Debug() << "Starting rename folder: folder_id=" << folder_id << ", new_name=\""
                  << new_name << "\", user_id=" << user_id;

        try {
            auto folder_result = co_await m_db_client->execSqlCoro(
                "SELECT id, user_id, parent_id, name, path, depth, item_count, created_at, updated_at "
                "FROM folders WHERE id = $1 AND user_id = $2",
                folder_id,
                user_id
            );
            if (folder_result.empty()) {
                co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
            }

            Folders folder(folder_result[0], -1);
            auto parent_id = folder.getValueOfParentId();
            if (folder.getValueOfName() != new_name &&
                co_await IsFolderNameExists(new_name, parent_id, user_id)) {
                co_return std::unexpected(ErrorInfo(ErrorCode::FolderAlreadyExists));
            }

            std::string parent_path = "/";
            if (parent_id > 0) {
                auto parent_result = co_await m_db_client->execSqlCoro(
                    "SELECT path FROM folders WHERE id = $1 AND user_id = $2",
                    parent_id,
                    user_id
                );
                if (parent_result.empty()) {
                    co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
                }
                parent_path = parent_result[0]["path"].as<std::string>();
            }

            auto old_prefix = folder.getValueOfPath();
            auto new_prefix = BuildFolderPath(parent_path, new_name);

            auto subtree_result = co_await m_db_client->execSqlCoro(
                "WITH RECURSIVE folder_tree AS ("
                "SELECT id, user_id, parent_id, name, path, depth, item_count, created_at, updated_at "
                "FROM folders WHERE id = $1 AND user_id = $2 "
                "UNION ALL "
                "SELECT f.id, f.user_id, f.parent_id, f.name, f.path, f.depth, "
                "f.item_count, f.created_at, f.updated_at "
                "FROM folders f INNER JOIN folder_tree ft ON f.parent_id = ft.id "
                "WHERE f.user_id = $3) "
                "SELECT id, user_id, parent_id, name, path, depth, item_count, created_at, updated_at "
                "FROM folder_tree ORDER BY depth ASC, id ASC",
                folder_id,
                user_id,
                user_id
            );

            std::vector<Folders> subtree;
            subtree.reserve(subtree_result.size());
            for (const auto& row : subtree_result) {
                subtree.emplace_back(row, -1);
            }

            std::shared_ptr<drogon::orm::Transaction> txn;
            try {
                txn = co_await m_db_client->newTransactionCoro();

                for (const auto& item : subtree) {
                    auto old_path = item.getValueOfPath();
                    auto new_path = new_prefix + old_path.substr(old_prefix.size());
                    if (item.getValueOfId() == folder_id) {
                        co_await txn->execSqlCoro(
                            "UPDATE folders SET name = $1, path = $2, updated_at = $3 "
                            "WHERE id = $4 AND user_id = $5",
                            new_name,
                            new_path,
                            trantor::Date::now(),
                            item.getValueOfId(),
                            user_id
                        );
                    } else {
                        co_await txn->execSqlCoro(
                            "UPDATE folders SET path = $1, updated_at = $2 WHERE id = $3 AND user_id = $4",
                            new_path,
                            trantor::Date::now(),
                            item.getValueOfId(),
                            user_id
                        );
                    }
                }

                std::unordered_map<uint64_t, std::string> folder_paths;
                folder_paths.reserve(subtree.size());
                for (const auto& item : subtree) {
                    auto old_path = item.getValueOfPath();
                    folder_paths[item.getValueOfId()] = new_prefix + old_path.substr(old_prefix.size());
                }

                if (!folder_paths.empty()) {
                    std::vector<uint64_t> folder_ids;
                    folder_ids.reserve(folder_paths.size());
                    for (const auto& [id, _] : folder_paths) {
                        folder_ids.push_back(id);
                    }
                    auto files_result = co_await txn->execSqlCoro(
                        "SELECT id, folder_id, name FROM files WHERE user_id = $1 AND folder_id IN (" +
                            disk::utils::BatchUtils::BuildSafeNumericInClause(folder_ids) + ")",
                        user_id
                    );
                    for (const auto& row : files_result) {
                        auto file_id = row["id"].as<uint64_t>();
                        auto file_folder_id = row["folder_id"].as<uint64_t>();
                        auto file_name = row["name"].as<std::string>();
                        auto path_it = folder_paths.find(file_folder_id);
                        if (path_it == folder_paths.end()) {
                            continue;
                        }
                        co_await txn->execSqlCoro(
                            "UPDATE files SET path = $1, updated_at = $2 WHERE id = $3 AND user_id = $4",
                            BuildFilePath(path_it->second, file_name),
                            trantor::Date::now(),
                            file_id,
                            user_id
                        );
                    }
                }
            } catch (const drogon::orm::DrogonDbException& e) {
                Logger::Error() << "Rename folder transaction failed (DB): " << e.base().what();
                if (txn) {
                    try {
                        txn->rollback();
                    } catch (const std::exception& rb_e) {
                        Logger::Error() << "Transaction rollback failed: " << rb_e.what();
                    }
                }
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to rename folder")
                );
            } catch (const std::exception& e) {
                Logger::Error() << "Rename folder transaction failed: " << e.what();
                if (txn) {
                    try {
                        txn->rollback();
                    } catch (const std::exception& rb_e) {
                        Logger::Error() << "Transaction rollback failed: " << rb_e.what();
                    }
                }
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to rename folder")
                );
            }

            RenameFolderResponse response;
            response.id = folder_id;
            response.name = new_name;
            response.path = new_prefix;
            response.updated_at = trantor::Date::now().toDbStringLocal();
            co_return response;
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "Folder not found or no permission: folder_id=" << folder_id << " - "
                     << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
        }
    }

    auto FolderService::FindAndValidateParent(uint64_t parent_id, uint64_t user_id) const
        -> drogon::Task<Result<Folders>> {

        try {
            CoroMapper<Folders> mapper(m_db_client);

            auto parent = co_await mapper.findOne(
                Criteria(Folders::Cols::_id, CompareOperator::EQ, parent_id)
            );

            /// 验证文件夹属于当前用户
            if (parent.getValueOfUserId() != user_id) {
                Logger::Warn() << "Parent folder does not belong to current user: parent_id=" << parent_id
                         << ", owner_id=" << parent.getValueOfUserId() << ", user_id=" << user_id;
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::FolderNotFound, "Parent folder not found")
                );
            }

            co_return parent;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "Parent folder does not exist: parent_id=" << parent_id << " - "
                     << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
        }
    }

    auto FolderService::IsFolderNameExists(
        const std::string& name,
        uint64_t parent_id,
        uint64_t user_id
    ) const -> drogon::Task<bool> {

        try {
            CoroMapper<Folders> mapper(m_db_client);

            auto count = co_await mapper.count(
                Criteria(Folders::Cols::_user_id, CompareOperator::EQ, user_id) &&
                Criteria(Folders::Cols::_parent_id, CompareOperator::EQ, parent_id) &&
                Criteria(Folders::Cols::_name, CompareOperator::EQ, name)
            );

            Logger::Debug() << "Checking folder name existence: name=\"" << name
                      << "\", parent_id=" << parent_id << " - "
                      << (count > 0 ? "exists" : "does not exist");

            co_return count > 0;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to check folder name: name=\"" << name << "\" - "
                      << e.base().what();
            co_return false;
        }
    }

    auto FolderService::IncrementParentItemCount(uint64_t parent_id) -> drogon::Task<void> {
        try {
            CoroMapper<Folders> mapper(m_db_client);

            auto parent = co_await mapper.findOne(
                Criteria(Folders::Cols::_id, CompareOperator::EQ, parent_id)
            );

            parent.setItemCount(parent.getValueOfItemCount() + 1);
            parent.setUpdatedAt(trantor::Date::now());

            co_await mapper.update(parent);
            Logger::Debug() << "Updated parent folder item_count: parent_id=" << parent_id
                      << ", new_count=" << parent.getValueOfItemCount();

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "Failed to update parent folder item_count: parent_id=" << parent_id
                     << " - " << e.base().what();
        }
    }

    auto FolderService::GetFolderTree(uint64_t user_id, uint64_t parent_id, int depth)
        -> drogon::Task<Result<FolderTreeNode>> {

        Logger::Debug() << "Starting get folder tree: user_id=" << user_id << ", parent_id=" << parent_id
                  << ", depth=" << depth;

        /// 1. 验证父文件夹归属（如果 parent_id > 0）
        if (parent_id > 0) {
            auto validate_result = co_await ValidateParentOwnership(parent_id, user_id);
            if (!validate_result) {
                Logger::Warn() << "Parent folder validation failed: parent_id=" << parent_id;
                co_return std::unexpected(validate_result.error());
            }
        }

        /// 2. 计算最大深度（-1 映射为 100）
        int max_depth = (depth == -1) ? 100 : depth;

        std::vector<FolderNodeData> nodes;

        try {
            auto result = co_await m_db_client->execSqlCoro(
                "WITH RECURSIVE folder_tree AS (" "SELECT id, name, parent_id, path, 0 AS level " "FROM folders " "WHERE user_id = $1 AND parent_id = $2 " "UNION ALL " "SELECT f.id, f.name, f.parent_id, f.path, ft.level + 1 " "FROM folders f " "INNER JOIN folder_tree ft ON f.parent_id = ft.id " "WHERE f.user_id = $3 AND ft.level < $4" ") " "SELECT id, name, parent_id FROM folder_tree ORDER BY path",
                user_id,
                parent_id,
                user_id,
                max_depth
            );

            if (result.size() == 0) {
                Logger::Debug() << "Folder tree is empty, returning root node";
                FolderTreeNode root;
                root.id = parent_id;
                root.name = (parent_id == 0) ? "根目录" : "";
                co_return root;
            }

            for (const auto& row : result) {
                FolderNodeData node;
                node.id = row["id"].as<uint64_t>();
                node.name = row["name"].as<std::string>();
                node.parent_id = row["parent_id"].as<uint64_t>();
                nodes.push_back(node);
            }

            Logger::Debug() << "Found " << nodes.size() << " folder nodes";

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to query folder tree: " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to get folder tree, please try again later"
            ));
        }

        /// 4. 构建树结构
        auto tree = BuildTreeFromFlatList(nodes, parent_id);

        co_return tree;
    }

    auto FolderService::ValidateParentOwnership(uint64_t parent_id, uint64_t user_id) const
        -> drogon::Task<Result<void>> {

        try {
            CoroMapper<Folders> mapper(m_db_client);

            auto parent = co_await mapper.findOne(
                Criteria(Folders::Cols::_id, CompareOperator::EQ, parent_id)
            );

            if (parent.getValueOfUserId() != user_id) {
                Logger::Warn() << "Parent folder does not belong to current user: parent_id=" << parent_id
                         << ", owner_id=" << parent.getValueOfUserId() << ", user_id=" << user_id;
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::FolderNotFound, "Parent folder not found")
                );
            }

            co_return {};

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "Parent folder does not exist: parent_id=" << parent_id << " - "
                     << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
        }
    }

    auto
    FolderService::BuildTreeFromFlatList(std::vector<FolderNodeData>& nodes, uint64_t root_id) const
        -> FolderTreeNode {

        /// 构建 parent_id -> children 映射
        std::unordered_map<uint64_t, std::vector<FolderNodeData>> children_map;

        for (auto& node : nodes) {
            children_map[node.parent_id].push_back(std::move(node));
        }

        /// 递归构建子树
        std::function<void(FolderTreeNode&, uint64_t)> build_children =
            [&children_map, &build_children](FolderTreeNode& parent, uint64_t parent_id) {
                auto it = children_map.find(parent_id);
                if (it == children_map.end()) {
                    return;
                }

                for (const auto& node_data : it->second) {
                    FolderTreeNode child;
                    child.id = node_data.id;
                    child.name = node_data.name;

                    build_children(child, node_data.id);
                    parent.children.push_back(std::move(child));
                }
            };

        /// 构建根节点
        FolderTreeNode root;
        root.id = root_id;
        root.name = (root_id == 0) ? "根目录" : "";

        build_children(root, root_id);

        return root;
    }

    auto FolderService::GetBreadcrumb(uint64_t folder_id, uint64_t user_id)
        -> drogon::Task<Result<BreadcrumbResponse>> {

        Logger::Debug() << "Starting get breadcrumb: folder_id=" << folder_id << ", user_id=" << user_id;

        /// 1. 特殊情况：根目录
        if (folder_id == 0) {
            BreadcrumbResponse response;
            response.path.push_back(BreadcrumbItem{ .id = 0, .name = "根目录" });
            co_return response;
        }

        /// 2. 查找文件夹并验证归属
        auto folder_result = co_await FindAndValidateParent(folder_id, user_id);
        if (!folder_result) {
            Logger::Warn() << "Folder validation failed: folder_id=" << folder_id;
            co_return std::unexpected(folder_result.error());
        }

        /// 3. 沿父链遍历
        std::vector<BreadcrumbItem> path;
        std::unordered_set<uint64_t> visited;
        auto current = *folder_result;

        while (true) {
            /// 检查深度限制
            if (path.size() >= 50) {
                Logger::Error() << "Breadcrumb depth limit exceeded: folder_id=" << folder_id;
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Folder hierarchy too deep")
                );
            }

            /// 检测循环引用
            if (visited.contains(current.getValueOfId())) {
                Logger::Error() << "Circular reference detected: folder_id=" << current.getValueOfId();
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Folder structure anomaly")
                );
            }
            visited.insert(current.getValueOfId());

            /// 添加当前文件夹到路径
            path.push_back(BreadcrumbItem{ .id = current.getValueOfId(), .name = current.getValueOfName() });

            /// 到达根目录
            if (current.getValueOfParentId() == 0) {
                break;
            }

            /// 获取父文件夹
            auto parent_result = co_await FindFolderById(current.getValueOfParentId());
            if (!parent_result) {
                Logger::Warn() << "Parent chain broken: folder_id=" << current.getValueOfId()
                         << ", missing_parent_id=" << current.getValueOfParentId();
                break;
            }
            current = *parent_result;
        }

        /// 4. 添加根目录并反转
        path.push_back(BreadcrumbItem{ .id = 0, .name = "根目录" });
        std::ranges::reverse(path);

        /// 5. 构建响应
        BreadcrumbResponse response;
        response.path = std::move(path);

        Logger::Debug() << "Breadcrumb retrieved successfully: folder_id=" << folder_id
                  << ", depth=" << response.path.size();

        co_return response;
    }

    auto FolderService::FindFolderById(uint64_t folder_id) const
        -> drogon::Task<std::optional<Folders>> {

        try {
            CoroMapper<Folders> mapper(m_db_client);

            auto folder = co_await mapper.findOne(
                Criteria(Folders::Cols::_id, CompareOperator::EQ, folder_id)
            );

            co_return folder;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Debug() << "Folder does not exist: folder_id=" << folder_id;
            co_return std::nullopt;
        }
    }

} ///< namespace disk::folder
