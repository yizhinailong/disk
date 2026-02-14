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
#include <string>

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

        drogon::orm::DbClientPtr m_db_client; ///< 数据库客户端
    };

} // namespace disk::folder
