/**
 * @file FolderService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件夹服务实现
 * @version 0.1
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "FolderService.hpp"

#include <algorithm>

namespace disk::folder {

    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::Folders;

    FolderService::FolderService(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
        LOG_DEBUG << "FolderService initialized";
    }

    auto FolderService::CreateFolder(CreateFolderRequest request, uint64_t user_id)
        -> drogon::Task<Result<CreateFolderResponse>> {

        LOG_DEBUG << "Starting create folder: name=\"" << request.name
                  << "\", parent_id=" << request.parent_id << ", user_id=" << user_id;

        // 1. 验证父文件夹（如果 parent_id > 0）
        std::string parent_path = "/";
        uint32_t parent_depth = 0;

        if (request.parent_id > 0) {
            auto parent_result = co_await FindAndValidateParent(request.parent_id, user_id);
            if (!parent_result) {
                LOG_WARN << "Parent folder validation failed: parent_id=" << request.parent_id;
                co_return std::unexpected(parent_result.error());
            }

            const auto& parent = *parent_result;
            parent_path = parent.getValueOfPath();
            parent_depth = parent.getValueOfDepth();
            LOG_DEBUG << "Parent folder validated: path=" << parent_path
                      << ", depth=" << parent_depth;
        }

        // 2. 检查同名文件夹是否已存在
        if (co_await IsFolderNameExists(request.name, request.parent_id, user_id)) {
            LOG_WARN << "Folder with same name already exists: name=\"" << request.name
                     << "\", parent_id=" << request.parent_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::FolderAlreadyExists));
        }

        // 3. 计算路径和深度
        std::string folder_path = parent_path + request.name + "/";
        uint32_t folder_depth = parent_depth + 1;

        LOG_DEBUG << "Calculated folder path: path=\"" << folder_path
                  << "\", depth=" << folder_depth;

        // 4. 创建文件夹记录
        Folders folder;
        folder.setUserId(user_id);
        folder.setParentId(request.parent_id);
        folder.setName(request.name);
        folder.setPath(folder_path);
        folder.setDepth(folder_depth);
        folder.setItemCount(0);
        folder.setCreatedAt(trantor::Date::now());
        folder.setUpdatedAt(trantor::Date::now());

        // 5. 插入数据库
        try {
            CoroMapper<Folders> mapper(m_db_client);
            folder = co_await mapper.insert(folder);
            LOG_INFO << "Folder created successfully: name=\"" << request.name
                     << "\" (ID: " << folder.getValueOfId() << ", user_id: " << user_id << ")";
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "文件夹创建失败: name=\"" << request.name << "\" - " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "创建文件夹失败，请稍后重试")
            );
        }

        // 6. 更新父文件夹的 item_count（如果 parent_id > 0）
        if (request.parent_id > 0) {
            co_await IncrementParentItemCount(request.parent_id);
        }

        // 7. 构造响应
        CreateFolderResponse response;
        response.id = folder.getValueOfId();
        response.name = folder.getValueOfName();
        response.parent_id = folder.getValueOfParentId();
        response.path = folder.getValueOfPath();
        response.created_at = folder.getValueOfCreatedAt().toDbStringLocal();

        co_return response;
    }

    auto FolderService::FindAndValidateParent(uint64_t parent_id, uint64_t user_id) const
        -> drogon::Task<Result<Folders>> {

        try {
            CoroMapper<Folders> mapper(m_db_client);

            auto parent = co_await mapper.findOne(
                Criteria(Folders::Cols::_id, CompareOperator::EQ, parent_id)
            );

            // 验证文件夹属于当前用户
            if (parent.getValueOfUserId() != user_id) {
                LOG_WARN << "父文件夹不属于当前用户: parent_id=" << parent_id
                         << ", owner_id=" << parent.getValueOfUserId() << ", user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound, "父文件夹不存在"));
            }

            co_return parent;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_WARN << "父文件夹不存在: parent_id=" << parent_id << " - " << e.base().what();
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

            LOG_DEBUG << "检查文件夹名称存在性: name=\"" << name << "\", parent_id=" << parent_id
                      << " - " << (count > 0 ? "存在" : "不存在");

            co_return count > 0;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "检查文件夹名称失败: name=\"" << name << "\" - " << e.base().what();
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
            LOG_DEBUG << "更新父文件夹 item_count: parent_id=" << parent_id
                      << ", new_count=" << parent.getValueOfItemCount();

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_WARN << "更新父文件夹 item_count 失败: parent_id=" << parent_id << " - "
                     << e.base().what();
        }
    }

    auto FolderService::GetFolderTree(uint64_t user_id, uint64_t parent_id, int depth)
        -> drogon::Task<Result<FolderTreeNode>> {

        LOG_DEBUG << "开始获取文件夹树: user_id=" << user_id << ", parent_id=" << parent_id
                  << ", depth=" << depth;

        // 1. 验证父文件夹归属（如果 parent_id > 0）
        if (parent_id > 0) {
            auto validate_result = co_await ValidateParentOwnership(parent_id, user_id);
            if (!validate_result) {
                LOG_WARN << "父文件夹验证失败: parent_id=" << parent_id;
                co_return std::unexpected(validate_result.error());
            }
        }

        // 2. 计算最大深度（-1 映射为 100）
        int max_depth = (depth == -1) ? 100 : depth;

        // 3. 调用存储过程获取文件夹树
        std::vector<FolderNodeData> nodes;

        try {
            auto result = co_await m_db_client->execSqlCoro(
                "CALL sp_get_folder_tree(?, ?, ?)",
                user_id,
                parent_id,
                max_depth
            );

            if (result.size() == 0) {
                LOG_DEBUG << "文件夹树为空，返回根节点";
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

            LOG_DEBUG << "查询到 " << nodes.size() << " 个文件夹节点";

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "查询文件夹树失败: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "获取文件夹树失败，请稍后重试")
            );
        }

        // 4. 构建树结构
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
                LOG_WARN << "父文件夹不属于当前用户: parent_id=" << parent_id
                         << ", owner_id=" << parent.getValueOfUserId() << ", user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound, "父文件夹不存在"));
            }

            co_return {};

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_WARN << "父文件夹不存在: parent_id=" << parent_id << " - " << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
        }
    }

    auto
    FolderService::BuildTreeFromFlatList(std::vector<FolderNodeData>& nodes, uint64_t root_id) const
        -> FolderTreeNode {

        // 构建 parent_id -> children 映射
        std::unordered_map<uint64_t, std::vector<FolderNodeData>> children_map;

        for (auto& node : nodes) {
            children_map[node.parent_id].push_back(std::move(node));
        }

        // 递归构建子树
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

        // 构建根节点
        FolderTreeNode root;
        root.id = root_id;
        root.name = (root_id == 0) ? "根目录" : "";

        build_children(root, root_id);

        return root;
    }

    auto FolderService::GetBreadcrumb(uint64_t folder_id, uint64_t user_id)
        -> drogon::Task<Result<BreadcrumbResponse>> {

        LOG_DEBUG << "开始获取面包屑导航: folder_id=" << folder_id << ", user_id=" << user_id;

        // 1. 特殊情况：根目录
        if (folder_id == 0) {
            BreadcrumbResponse response;
            response.path.push_back({ 0, "根目录" });
            co_return response;
        }

        // 2. 查找文件夹并验证归属
        auto folder_result = co_await FindAndValidateParent(folder_id, user_id);
        if (!folder_result) {
            LOG_WARN << "文件夹验证失败: folder_id=" << folder_id;
            co_return std::unexpected(folder_result.error());
        }

        // 3. 沿父链遍历
        std::vector<BreadcrumbItem> path;
        std::unordered_set<uint64_t> visited;
        auto current = *folder_result;

        while (true) {
            // 检查深度限制
            if (path.size() >= 50) {
                LOG_ERROR << "面包屑深度超出限制: folder_id=" << folder_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "文件夹层级过深"));
            }

            // 检测循环引用
            if (visited.contains(current.getValueOfId())) {
                LOG_ERROR << "检测到循环引用: folder_id=" << current.getValueOfId();
                co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "文件夹结构异常"));
            }
            visited.insert(current.getValueOfId());

            // 添加当前文件夹到路径
            path.push_back({ current.getValueOfId(), current.getValueOfName() });

            // 到达根目录
            if (current.getValueOfParentId() == 0) {
                break;
            }

            // 获取父文件夹
            auto parent_result = co_await FindFolderById(current.getValueOfParentId());
            if (!parent_result) {
                LOG_WARN << "父链断裂: folder_id=" << current.getValueOfId()
                         << ", missing_parent_id=" << current.getValueOfParentId();
                break;
            }
            current = *parent_result;
        }

        // 4. 添加根目录并反转
        path.push_back({ 0, "根目录" });
        std::ranges::reverse(path);

        // 5. 构建响应
        BreadcrumbResponse response;
        response.path = std::move(path);

        LOG_DEBUG << "面包屑导航获取成功: folder_id=" << folder_id
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
            LOG_DEBUG << "文件夹不存在: folder_id=" << folder_id;
            co_return std::nullopt;
        }
    }

} // namespace disk::folder
