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

#include "FileListCache.hpp"
#include "FileRepository.hpp"
#include "TransactionRunner.hpp"
#include "utils/BatchUtils.hpp"

namespace disk::folder {

    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::Folders;

    FolderService::FolderService(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
        Logger::Debug(disk::utils::ServiceRuntimeLogContext()) << "Service initialized: service=folder";
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
    } // namespace

    auto FolderService::CreateFolder(
        CreateFolderRequest request,
        uint64_t user_id,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<CreateFolderResponse>> {

        Logger::Debug(log_context) << "Starting create folder: name=\"" << request.name
                                   << "\", parent_id=" << request.parent_id
                                   << ", user_id=" << user_id;

        Folders folder;
        disk::file::TransactionRunner transaction_runner(
            m_db_client,
            ErrorInfo(ErrorCode::InternalError, "Failed to create folder, please try again later"),
            log_context
        );
        auto transaction_result = co_await transaction_runner.Run(
            [&](const drogon::orm::DbClientPtr& transaction) -> drogon::Task<Result<void>> {
                std::string parent_path = "/";
                uint32_t parent_depth = 0;

                if (request.parent_id > 0) {
                    auto parent_updated = co_await m_folder_repository.ApplyItemCountDelta(
                        transaction,
                        request.parent_id,
                        user_id,
                        1,
                        trantor::Date::now()
                    );
                    if (!parent_updated) {
                        co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
                    }

                    auto parent = co_await m_folder_repository.FindOwnedFolder(
                        transaction,
                        request.parent_id,
                        user_id
                    );
                    if (!parent.has_value()) {
                        co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
                    }
                    parent_path = parent->getValueOfPath();
                    parent_depth = parent->getValueOfDepth();
                }

                const auto now = trantor::Date::now();
                folder.setUserId(user_id);
                folder.setParentId(request.parent_id);
                folder.setName(request.name);
                folder.setPath(BuildFolderPath(parent_path, request.name));
                folder.setDepth(parent_depth + 1);
                folder.setItemCount(0);
                folder.setCreatedAt(now);
                folder.setUpdatedAt(now);

                auto inserted = co_await m_folder_repository.InsertIfNameAvailable(
                    transaction,
                    folder
                );
                if (!inserted.has_value()) {
                    co_return std::unexpected(ErrorInfo(ErrorCode::FolderAlreadyExists));
                }
                folder = std::move(inserted.value());
                co_return {};
            }
        );
        if (!transaction_result) {
            if (transaction_result.error().code == ErrorCode::FolderAlreadyExists) {
                Logger::Warn(log_context)
                    << "Folder with same name already exists: name=\"" << request.name
                    << "\", parent_id=" << request.parent_id;
            } else if (transaction_result.error().code == ErrorCode::FolderNotFound) {
                Logger::Warn(log_context)
                    << "Parent folder does not exist or belongs to another user: parent_id="
                    << request.parent_id << ", user_id=" << user_id;
            } else {
                Logger::Error(log_context) << "Folder creation transaction failed";
            }
            co_return std::unexpected(transaction_result.error());
        }

        Logger::Info(log_context)
            << "Folder created successfully: name=\"" << request.name << "\" (ID: "
            << folder.getValueOfId() << ", user_id: " << user_id << ")";

        co_await disk::file::FileListCache::Invalidate(
            m_redis_service,
            user_id,
            log_context
        );

        CreateFolderResponse response;
        response.id = folder.getValueOfId();
        response.name = folder.getValueOfName();
        response.parent_id = folder.getValueOfParentId();
        response.path = folder.getValueOfPath();
        response.created_at = folder.getValueOfCreatedAt().toDbStringLocal();

        co_return response;
    }

    auto FolderService::Rename(
        uint64_t folder_id,
        std::string new_name,
        uint64_t user_id,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<RenameFolderResponse>> {

        Logger::Debug(log_context)
            << "Starting rename folder: folder_id=" << folder_id << ", new_name=\"" << new_name
            << "\", user_id=" << user_id;

        try {
            auto folder = co_await m_folder_repository.FindOwnedFolder(m_db_client, folder_id, user_id);
            if (!folder) {
                Logger::Warn(log_context)
                    << "Folder not found or no permission: folder_id=" << folder_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
            }

            auto parent_id = folder->getValueOfParentId();
            if (folder->getValueOfName() != new_name &&
                co_await IsFolderNameExists(new_name, parent_id, user_id, log_context)) {
                co_return std::unexpected(ErrorInfo(ErrorCode::FolderAlreadyExists));
            }

            std::string parent_path = "/";
            if (parent_id > 0) {
                auto parent = co_await m_folder_repository.FindOwnedFolder(m_db_client, parent_id, user_id);
                if (!parent) {
                    co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
                }
                parent_path = parent->getValueOfPath();
            }

            auto old_prefix = folder->getValueOfPath();
            auto new_prefix = BuildFolderPath(parent_path, new_name);
            auto subtree = co_await m_folder_repository.FetchFolderSubtree(m_db_client, folder_id, user_id);

            std::shared_ptr<drogon::orm::Transaction> txn;
            try {
                txn = co_await disk::file::TransactionRunner::Begin(m_db_client);

                for (const auto& item : subtree) {
                    auto old_path = item.getValueOfPath();
                    auto new_path = new_prefix + old_path.substr(old_prefix.size());
                    if (item.getValueOfId() == folder_id) {
                        co_await m_folder_repository.RenameFolderPath(
                            txn,
                            item.getValueOfId(),
                            user_id,
                            new_name,
                            new_path,
                            trantor::Date::now()
                        );
                    } else {
                        co_await m_folder_repository.UpdateFolderPath(
                            txn,
                            item.getValueOfId(),
                            user_id,
                            new_path,
                            trantor::Date::now()
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
                    disk::file::FileRepository file_repository;
                    auto files = co_await file_repository.FetchFilesInFolders(txn, folder_ids, user_id);
                    for (const auto& file : files) {
                        auto path_it = folder_paths.find(file.getValueOfFolderId());
                        if (path_it == folder_paths.end()) {
                            continue;
                        }
                        co_await file_repository.UpdateFilePath(
                            txn,
                            file.getValueOfId(),
                            user_id,
                            BuildFilePath(path_it->second, file.getValueOfName()),
                            trantor::Date::now()
                        );
                    }
                }
            } catch (const drogon::orm::DrogonDbException& e) {
                Logger::Error(log_context)
                    << "Rename folder transaction failed (DB): " << e.base().what();
                if (txn) {
                    try {
                        txn->rollback();
                    } catch (const std::exception& rb_e) {
                        Logger::Error(log_context)
                            << "Transaction rollback failed: " << rb_e.what();
                    }
                }
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to rename folder")
                );
            } catch (const std::exception& e) {
                Logger::Error(log_context) << "Rename folder transaction failed: " << e.what();
                if (txn) {
                    try {
                        txn->rollback();
                    } catch (const std::exception& rb_e) {
                        Logger::Error(log_context)
                            << "Transaction rollback failed: " << rb_e.what();
                    }
                }
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to rename folder")
                );
            }

            auto commit_result = co_await disk::file::TransactionRunner::Commit(
                txn,
                log_context
            );
            if (!commit_result) {
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to rename folder")
                );
            }
            co_await disk::file::FileListCache::Invalidate(
                m_redis_service,
                user_id,
                log_context
            );

            RenameFolderResponse response;
            response.id = folder_id;
            response.name = new_name;
            response.path = new_prefix;
            response.updated_at = trantor::Date::now().toDbStringLocal();
            co_return response;
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn(log_context)
                << "Folder not found or no permission: folder_id=" << folder_id << " - "
                << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
        }
    }

    auto FolderService::IsFolderNameExists(
        const std::string& name,
        uint64_t parent_id,
        uint64_t user_id,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<bool> {

        try {
            CoroMapper<Folders> mapper(m_db_client);

            auto count = co_await mapper.count(
                Criteria(Folders::Cols::_user_id, CompareOperator::EQ, user_id) &&
                Criteria(Folders::Cols::_parent_id, CompareOperator::EQ, parent_id) &&
                Criteria(Folders::Cols::_name, CompareOperator::EQ, name)
            );

            Logger::Debug(log_context)
                << "Checking folder name existence: name=\"" << name
                << "\", parent_id=" << parent_id << " - "
                << (count > 0 ? "exists" : "does not exist");

            co_return count > 0;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error(log_context)
                << "Failed to check folder name: name=\"" << name << "\" - "
                << e.base().what();
            co_return false;
        }
    }

    auto FolderService::GetFolderTree(
        uint64_t user_id,
        uint64_t parent_id,
        int depth,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<FolderTreeNode>> {

        Logger::Debug(log_context) << "Starting get folder tree: user_id=" << user_id
                                   << ", parent_id=" << parent_id << ", depth=" << depth;

        /// 1. 验证父文件夹归属（如果 parent_id > 0）
        if (parent_id > 0) {
            auto validate_result = co_await ValidateParentOwnership(
                parent_id,
                user_id,
                log_context
            );
            if (!validate_result) {
                Logger::Warn(log_context)
                    << "Parent folder validation failed: parent_id=" << parent_id;
                co_return std::unexpected(validate_result.error());
            }
        }

        /// 2. 计算最大深度（-1 映射为 100）
        int max_depth = (depth == -1) ? 100 : depth;

        std::vector<FolderNodeData> nodes;

        try {
            nodes = co_await m_folder_repository.FetchFolderTreeRows(
                m_db_client,
                user_id,
                parent_id,
                max_depth
            );

            if (nodes.empty()) {
                Logger::Debug(log_context) << "Folder tree is empty, returning root node";
                FolderTreeNode root;
                root.id = parent_id;
                root.name = (parent_id == 0) ? "根目录" : "";
                co_return root;
            }

            Logger::Debug(log_context) << "Found " << nodes.size() << " folder nodes";

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error(log_context) << "Failed to query folder tree: " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to get folder tree, please try again later"
            ));
        }

        /// 4. 构建树结构
        auto tree = BuildTreeFromFlatList(nodes, parent_id);

        co_return tree;
    }

    auto FolderService::ValidateParentOwnership(
        uint64_t parent_id,
        uint64_t user_id,
        disk::utils::LogContext log_context
    ) const
        -> drogon::Task<Result<void>> {

        try {
            auto parent = co_await m_folder_repository.FindOwnedFolder(m_db_client, parent_id, user_id);
            if (!parent) {
                Logger::Warn(log_context)
                    << "Parent folder does not exist or belongs to another user: parent_id="
                    << parent_id << ", user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
            }

            co_return {};

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn(log_context)
                << "Parent folder does not exist: parent_id=" << parent_id << " - "
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

    auto FolderService::GetBreadcrumb(
        uint64_t folder_id,
        uint64_t user_id,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<BreadcrumbResponse>> {

        Logger::Debug(log_context)
            << "Starting get breadcrumb: folder_id=" << folder_id << ", user_id=" << user_id;

        if (folder_id == 0) {
            BreadcrumbResponse response;
            response.path.push_back(BreadcrumbItem{ .id = 0, .name = "根目录" });
            co_return response;
        }

        try {
            auto path = co_await m_folder_repository.FetchBreadcrumbRows(m_db_client, folder_id, user_id);

            if (path.empty()) {
                Logger::Warn(log_context)
                    << "Breadcrumb folder not found or no permission: folder_id=" << folder_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
            }

            path.push_back(BreadcrumbItem{ .id = 0, .name = "根目录" });
            std::ranges::reverse(path);

            BreadcrumbResponse response;
            response.path = std::move(path);

            Logger::Debug(log_context)
                << "Breadcrumb retrieved successfully: folder_id=" << folder_id
                << ", depth=" << response.path.size();

            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn(log_context)
                << "Breadcrumb query failed: folder_id=" << folder_id << " - "
                << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Failed to retrieve breadcrumb"));
        }
    }

} // namespace disk::folder
