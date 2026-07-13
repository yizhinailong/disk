/**
 * @file FileMutationService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件变更服务
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "dtos/FileDto.hpp"
#include "models/Files.hpp"
#include "services/FileRepository.hpp"
#include "services/FolderRepository.hpp"
#include "services/RedisService.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::storage {
    class IBlobStorage;
}

namespace disk::file {

    /**
     * @brief 文件变更服务类
     *
     * 提供文件变更相关的业务逻辑：
     * - 文件重命名
     * - 文件移动
     * - 文件复制
     * - 文件删除（移入回收站）
     */
    class FileMutationService {
    public:
        /**
         * @brief 构造函数
         * @param db_client 数据库客户端
         * @param storage 文件存储接口
         */
        explicit FileMutationService(drogon::orm::DbClientPtr db_client, storage::IBlobStorage* storage);
        ~FileMutationService() = default;
        FileMutationService(const FileMutationService&) = delete;
        auto operator=(const FileMutationService&) -> FileMutationService& = delete;
        FileMutationService(FileMutationService&&) = delete;
        auto operator=(FileMutationService&&) -> FileMutationService& = delete;

        /**
         * @brief 重命名文件
         *
         * 业务规则：
         * - 验证文件存在且属于用户
         * - 检查新文件名是否与同目录下其他文件冲突
         * - 更新文件名和更新时间
         *
         * @param file_id 文件 ID
         * @param new_name 新文件名
         * @param user_id 用户 ID
         * @return drogon::Task<Result<RenameResponse>> 成功返回重命名后的文件信息，失败返回错误
         */
        [[nodiscard]]
        auto Rename(uint64_t file_id, std::string new_name, uint64_t user_id)
            -> drogon::Task<Result<RenameResponse>>;

        /**
         * @brief 移动文件到目标文件夹
         *
         * 业务规则：
         * - 验证目标文件夹存在且属于用户
         * - 验证每个文件存在且属于用户
         * - 检查目标文件夹是否存在同名文件
         * - 更新文件的 folder_id
         *
         * @param request 移动请求
         * @param user_id 用户 ID
         * @return drogon::Task<Result<MoveResponse>> 成功返回移动统计，失败返回错误
         */
        [[nodiscard]]
        auto Move(MoveRequest request, uint64_t user_id) -> drogon::Task<Result<MoveResponse>>;

        /**
         * @brief 复制文件到目标文件夹
         *
         * 业务规则：
         * - 验证目标文件夹存在且属于用户
         * - 计算总复制大小，检查存储配额
         * - 验证每个文件存在且属于用户
         * - 检查目标文件夹是否存在同名文件
         * - 创建新文件记录（复用 content_id）
         * - 增加 file_contents.ref_count（不复制物理文件）
         * - 更新用户存储使用量
         *
         * @param request 复制请求
         * @param user_id 用户 ID
         * @return drogon::Task<Result<CopyResponse>> 成功返回复制统计和ID映射，失败返回错误
         */
        [[nodiscard]]
        auto Copy(CopyRequest request, uint64_t user_id) -> drogon::Task<Result<CopyResponse>>;

        /**
         * @brief 删除文件（移入回收站）
         *
         * 业务规则：
         * - 验证每个文件存在且属于用户
         * - 创建 trash 记录保存文件元数据
         * - item_data 包含 content_id 和 mime_type（用于恢复）
         * - 删除原始 files 记录
         * - 不更新 storage_used（回收站项目仍计入配额）
         * - 不减少 file_contents.ref_count（彻底删除时才减少）
         *
         * @param request 删除请求
         * @param user_id 用户 ID
         * @return drogon::Task<Result<DeleteResponse>> 成功返回删除统计，失败返回错误
         */
        [[nodiscard]]
        auto Delete(DeleteRequest request, uint64_t user_id)
            -> drogon::Task<Result<DeleteResponse>>;

    private:
        /**
         * @brief 检查并扣除存储配额（事务版）
         *
         * @param client 数据库客户端
         * @param user_id 用户 ID
         * @param file_size 需要的存储空间
         * @return drogon::Task<Result<void>> 成功或配额不足错误
         */
        [[nodiscard]]
        auto CheckStorageQuota(
            const drogon::orm::DbClientPtr& client,
            uint64_t user_id,
            uint64_t file_size
        ) const -> drogon::Task<Result<void>>;

        /**
         * @brief 更新存储使用量（事务版）
         *
         * @param client 数据库客户端
         * @param user_id 用户 ID
         * @param delta 存储增量
         */
        auto UpdateStorageUsed(
            const drogon::orm::DbClientPtr& client,
            uint64_t user_id,
            int64_t delta
        ) -> drogon::Task<void>;

        /**
         * @brief 批量插入复制文件记录并获取新 ID（事务版）
         *
         * @param client 数据库客户端
         * @param user_id 用户 ID
         * @param target_folder_id 目标文件夹 ID
         * @param valid_items 待插入的文件列表
         * @return 成功返回 (old_id, new_id) 映射；失败返回错误并由调用方回滚事务
         */
        [[nodiscard]]
        auto InsertCopiedFiles(
            const drogon::orm::DbClientPtr& client,
            uint64_t user_id,
            uint64_t target_folder_id,
            const std::vector<std::pair<uint64_t, const drogon_model::disk::Files*>>& valid_items
        ) -> drogon::Task<Result<std::vector<std::pair<uint64_t, uint64_t>>>>;

        [[nodiscard]]
        static auto ExtractExtension(const std::string& filename) -> std::string;

        [[nodiscard]]
        auto IsFilenameExists(
            uint64_t folder_id,
            const std::string& filename,
            uint64_t user_id
        ) const -> drogon::Task<bool>;

        /**
         * @brief Invalidate file list cache for specified user and folders
         *
         * @param user_id User ID
         * @param folder_ids Folder IDs whose file list caches should be invalidated
         * @return drogon::Task<void>
         */
        auto InvalidateFileListCache(uint64_t user_id, const std::vector<uint64_t>& folder_ids)
            -> drogon::Task<void>;

        drogon::orm::DbClientPtr m_db_client;                                      ///< 数据库客户端
        FileRepository m_file_repository;                                           ///< 文件持久化原语
        disk::folder::FolderRepository m_folder_repository;                         ///< 文件夹持久化原语
        storage::IBlobStorage* m_storage{};                                          ///< 最终 Blob 存储接口
        std::shared_ptr<disk::services::RedisService> m_redis_service{disk::services::RedisService::GetInstance()};  ///< Redis 服务
    };

} ///< namespace disk::file
