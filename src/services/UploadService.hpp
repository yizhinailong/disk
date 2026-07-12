/**
 * @file UploadService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件上传服务
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <drogon/orm/DbClient.h>
#include <trantor/utils/Date.h>

#include "dtos/FileDto.hpp"
#include "services/RedisService.hpp"
#include "models/Files.hpp"
#include "models/UploadTasks.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::storage {
    class IFileStorage;
}

namespace disk::file {

    /**
     * @brief 文件上传服务类
     *
     * 提供文件上传相关的业务逻辑：
     * - 初始化上传（配额检查、秒传检测、断点续传）
     * - 上传分片（分片验证、临时文件写入）
     * - 完成上传（分片组装、去重、文件记录创建）
     * - 取消上传（清理临时文件、删除上传任务）
     */
    class UploadService {
    public:
        /**
         * @brief 构造函数
         * @param db_client 数据库客户端
         * @param storage 文件存储接口
         */
        explicit UploadService(drogon::orm::DbClientPtr db_client, storage::IFileStorage* storage);
        ~UploadService() = default;
        UploadService(const UploadService&) = delete;
        auto operator=(const UploadService&) -> UploadService& = delete;
        UploadService(UploadService&&) = delete;
        auto operator=(UploadService&&) -> UploadService& = delete;

        /**
         * @brief 初始化上传
         *
         * 业务规则：
         * - 检查存储配额（storage_used + file_size <= storage_quota）
         * - 检测秒传：查找 FileContents 中是否存在相同 hash 的文件
         * - 检测断点续传：查找用户是否存在相同 hash 且未完成的上传任务
         * - 创建新的上传任务记录
         *
         * @param request 初始化上传请求
         * @param user_id 用户 ID
         * @return drogon::Task<Result<InitUploadResponse>> 成功返回上传会话信息，失败返回错误
         */
        [[nodiscard]]
        auto InitUpload(InitUploadRequest request, uint64_t user_id)
            -> drogon::Task<Result<InitUploadResponse>>;

        /**
         * @brief 上传分片
         *
         * 业务规则：
         * - 验证上传任务存在且属于当前用户
         * - 验证任务未过期
         * - 验证分片索引有效
         * - 计算并验证分片 MD5
         * - 写入临时文件
         * - 更新已上传分片列表
         *
         * @param upload_id 上传会话 ID
         * @param chunk_index 分片索引
         * @param chunk_hash 分片 MD5 哈希
         * @param chunk_data 分片数据
         * @param user_id 用户 ID
         * @return drogon::Task<Result<UploadChunkResponse>> 成功返回分片上传结果，失败返回错误
         */
        [[nodiscard]]
        auto UploadChunk(
            std::string upload_id,
            uint32_t chunk_index,
            std::string chunk_hash,
            std::string_view chunk_data,
            uint64_t user_id
        ) -> drogon::Task<Result<UploadChunkResponse>>;

        /**
         * @brief 完成上传
         *
         * 业务规则：
         * - 验证所有分片已上传
         * - 顺序组装分片到临时文件
         * - 验证最终文件 MD5
         * - 检查去重（FileContents）
         * - 创建 Files 记录
         * - 更新用户存储使用量
         * - 清理上传任务和临时文件
         *
         * @param upload_id 上传会话 ID
         * @param user_id 用户 ID
         * @return drogon::Task<Result<CompleteUploadResponse>> 成功返回文件信息，失败返回错误
         */
        [[nodiscard]]
        auto CompleteUpload(std::string upload_id, uint64_t user_id)
            -> drogon::Task<Result<CompleteUploadResponse>>;

        /**
         * @brief 取消上传
         *
         * 业务规则：
         * - 验证上传任务存在且属于当前用户
         * - 删除临时文件目录
         * - 删除上传任务记录
         *
         * @param upload_id 上传会话 ID
         * @param user_id 用户 ID
         * @return drogon::Task<Result<void>> 成功返回空，失败返回错误
         */
        [[nodiscard]]
        auto CancelUpload(std::string upload_id, uint64_t user_id) -> drogon::Task<Result<void>>;

    private:
        /**
         * @brief 上传任务缓存条目
         *
         * 缓存 UploadChunk 热路径需要的不可变元数据，避免每个分片都访问数据库。
         */
        struct UploadTaskCacheEntry {
            uint64_t user_id = 0;
            uint64_t file_size = 0;
            uint32_t chunk_size = 0;
            uint32_t total_chunks = 0;
            trantor::Date expires_at;
            int status = 0;
            std::string file_hash;
            std::string filename;
            uint64_t parent_id = 0;
            std::chrono::steady_clock::time_point cache_expires_at;
        };

        static constexpr auto UPLOAD_TASK_CACHE_TTL = std::chrono::seconds(60);
        static constexpr double UPLOAD_TASK_CACHE_MAINTENANCE_INTERVAL_SECONDS = 60.0;

        /// ── 原有私有方法（使用 m_db_client） ──

        [[nodiscard]]
        static auto BuildUploadTaskCacheEntry(const drogon_model::disk::UploadTasks& task)
            -> UploadTaskCacheEntry;

        [[nodiscard]]
        auto TryGetUploadTaskCacheEntry(const std::string& upload_id, uint64_t user_id)
            -> std::optional<UploadTaskCacheEntry>;

        auto CacheUploadTaskEntry(const std::string& upload_id, UploadTaskCacheEntry entry)
            -> void;

        auto InvalidateUploadTaskCache(const std::string& upload_id) -> void;

        auto StartUploadTaskCacheMaintenance() -> void;

        auto EvictExpiredUploadTaskCacheEntries() -> void;

        [[nodiscard]]
        auto CheckStorageQuota(uint64_t user_id, uint64_t file_size) const
            -> drogon::Task<Result<void>>;

        [[nodiscard]]
        auto ReserveStorageQuota(uint64_t user_id, uint64_t file_size) const
            -> drogon::Task<Result<void>>;

        auto ReleaseReservedQuota(uint64_t user_id, uint64_t reserved_bytes)
            -> drogon::Task<void>;

        [[nodiscard]]
        auto FindUploadTask(const std::string& upload_id, uint64_t user_id) const
            -> drogon::Task<Result<drogon_model::disk::UploadTasks>>;

        auto UpdateStorageUsed(uint64_t user_id, int64_t delta) -> drogon::Task<void>;

        [[nodiscard]]
        auto
        IsFilenameExists(uint64_t folder_id, const std::string& filename, uint64_t user_id) const
            -> drogon::Task<bool>;

        [[nodiscard]]
        static auto ExtractExtension(const std::string& filename) -> std::string;

        [[nodiscard]]
        static auto IsImageMimeType(const std::string& mime_type) -> bool;

        /// ── 事务感知辅助方法（接受 DbClientPtr，可传入事务或普通连接） ──

        /**
         * @brief 检查并扣除存储配额（事务版）
         *
         * 在指定数据库连接（事务或普通连接）上执行配额检查与扣除。
         * 事务场景下，扣除操作可随事务回滚。
         *
         * @param client 数据库客户端（事务或普通连接）
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
         * @param client 数据库客户端（事务或普通连接）
         * @param user_id 用户 ID
         * @param delta 存储增量（正数为增加，负数为减少）
         */
        auto UpdateStorageUsed(
            const drogon::orm::DbClientPtr& client,
            uint64_t user_id,
            int64_t delta
        ) -> drogon::Task<void>;

        [[nodiscard]]
        auto IsFilenameExists(
            const drogon::orm::DbClientPtr& client,
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
        storage::IFileStorage* m_storage{};                                          ///< 文件存储接口
        std::shared_ptr<disk::services::RedisService> m_redis_service{disk::services::RedisService::GetInstance()};  ///< Redis 服务
        std::unordered_map<std::string, UploadTaskCacheEntry> m_upload_task_cache; ///< 上传任务元数据缓存
        std::shared_mutex m_upload_task_cache_mutex;                               ///< 上传任务缓存读写锁
    };

} ///< namespace disk::file
