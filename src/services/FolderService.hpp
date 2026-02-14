/**
 * @file FolderService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件夹服务
 * @version 0.1
 * @date 2026-02-14
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

#include "dtos/FolderDto.hpp"
#include "models/Folders.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::folder {

    /**
     * @brief 文件夹服务类
     *
     * 提供文件夹管理相关的业务逻辑：
     * - 创建文件夹
     */
    class FolderService {
    public:
        /**
         * @brief 构造函数
         * @param db_client 数据库客户端
         */
        explicit FolderService(drogon::orm::DbClientPtr db_client);
        ~FolderService() = default;
        FolderService(const FolderService&) = delete;
        auto operator=(const FolderService&) -> FolderService& = delete;
        FolderService(FolderService&&) = default;
        auto operator=(FolderService&&) -> FolderService& = default;

        /**
         * @brief 创建文件夹
         *
         * 业务规则：
         * - 如果 parent_id > 0，验证父文件夹存在且属于用户
         * - 检查同一父目录下是否存在同名文件夹
         * - 计算路径：根目录 "/" + name + "/"，子目录 parent_path + name + "/"
         * - 计算深度：根目录为 0，子目录为 parent_depth + 1
         * - 创建成功后，如有父目录则更新其 item_count
         *
         * @param request 创建文件夹请求
         * @param user_id 用户 ID
         * @return drogon::Task<Result<CreateFolderResponse>> 成功返回文件夹信息，失败返回错误
         */
        [[nodiscard]]
        auto CreateFolder(CreateFolderRequest request, uint64_t user_id)
            -> drogon::Task<Result<CreateFolderResponse>>;

        /**
         * @brief 获取文件夹树
         *
         * 业务规则：
         * - 如果 parent_id > 0，验证父文件夹存在且属于用户
         * - 调用存储过程 sp_get_folder_tree 获取文件夹树
         * - depth=-1 表示无限深度（实际限制为100层）
         *
         * @param user_id 用户 ID
         * @param parent_id 起始文件夹 ID（0 表示根目录）
         * @param depth 深度限制（-1 表示无限）
         * @return drogon::Task<Result<FolderTreeNode>> 成功返回文件夹树，失败返回错误
         */
        [[nodiscard]]
        auto GetFolderTree(uint64_t user_id, uint64_t parent_id, int depth)
            -> drogon::Task<Result<FolderTreeNode>>;

        /**
         * @brief 获取面包屑导航路径
         *
         * 业务规则：
         * - folder_id == 0 返回仅包含根目录的面包屑
         * - 验证文件夹存在且属于当前用户
         * - 沿父链向上遍历直到到达根目录（parent_id == 0）
         * - 深度限制 50 层，超出返回 InternalError
         * - 检测循环引用，发现返回 InternalError
         * - 父链断裂时返回部分路径并记录警告日志
         *
         * @param folder_id 文件夹 ID（0 表示根目录）
         * @param user_id 用户 ID
         * @return drogon::Task<Result<BreadcrumbResponse>> 成功返回面包屑路径，失败返回错误
         */
        [[nodiscard]]
        auto GetBreadcrumb(uint64_t folder_id, uint64_t user_id)
            -> drogon::Task<Result<BreadcrumbResponse>>;

    private:
        /**
         * @brief 查找父文件夹并验证归属
         *
         * @param parent_id 父文件夹 ID
         * @param user_id 用户 ID
         * @return drogon::Task<Result<drogon_model::disk::Folders>> 成功返回父文件夹，失败返回错误
         */
        [[nodiscard]]
        auto FindAndValidateParent(uint64_t parent_id, uint64_t user_id) const
            -> drogon::Task<Result<drogon_model::disk::Folders>>;

        /**
         * @brief 检查文件夹名称是否已存在
         *
         * @param name 文件夹名称
         * @param parent_id 父文件夹 ID
         * @param user_id 用户 ID
         * @return drogon::Task<bool> 是否存在同名文件夹
         */
        [[nodiscard]]
        auto IsFolderNameExists(const std::string& name, uint64_t parent_id, uint64_t user_id) const
            -> drogon::Task<bool>;

        /**
         * @brief 更新父文件夹的 item_count
         *
         * @param parent_id 父文件夹 ID
         * @return drogon::Task<void>
         */
        auto IncrementParentItemCount(uint64_t parent_id) -> drogon::Task<void>;

        /**
         * @brief 验证父文件夹归属
         *
         * @param parent_id 父文件夹 ID
         * @param user_id 用户 ID
         * @return drogon::Task<Result<void>> 成功返回空，失败返回错误
         */
        [[nodiscard]]
        auto ValidateParentOwnership(uint64_t parent_id, uint64_t user_id) const
            -> drogon::Task<Result<void>>;

        /**
         * @brief 从扁平列表构建树结构
         *
         * @param nodes 文件夹节点数据列表
         * @param root_id 根节点 ID
         * @return FolderTreeNode 构建好的树结构
         */
        [[nodiscard]]
        auto BuildTreeFromFlatList(std::vector<FolderNodeData>& nodes, uint64_t root_id) const
            -> FolderTreeNode;

        /**
         * @brief 根据ID查找文件夹（不验证归属）
         *
         * @param folder_id 文件夹 ID
         * @return drogon::Task<std::optional<Folders>> 成功返回文件夹，失败返回空
         */
        [[nodiscard]]
        auto FindFolderById(uint64_t folder_id) const
            -> drogon::Task<std::optional<drogon_model::disk::Folders>>;

        drogon::orm::DbClientPtr m_db_client; ///< 数据库客户端
    };

} // namespace disk::folder
