/**
 * @file FileService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件服务
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
#include "models/Files.hpp"
#include "models/UploadTasks.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::storage {
    class IFileStorage;
}

namespace disk::file {

    /**
     * @brief 文件服务类
     *
     * 提供文件管理相关的业务逻辑：
     * - 初始化上传（配额检查、秒传检测、断点续传）
     * - 上传分片（分片验证、临时文件写入）
     * - 完成上传（分片组装、去重、文件记录创建）
     * - 取消上传（清理临时文件、删除上传任务）
     * - 文件列表、详情、下载
     * - 文件重命名、移动、复制、删除（移入回收站）
     * - 文件搜索
     */
    class FileService {
    public:
        /**
         * @brief 构造函数
         * @param db_client 数据库客户端
         * @param storage 文件存储接口
         */
        explicit FileService(drogon::orm::DbClientPtr db_client, storage::IFileStorage* storage);
        ~FileService() = default;
        FileService(const FileService&) = delete;
        auto operator=(const FileService&) -> FileService& = delete;
        FileService(FileService&&) = delete;
        auto operator=(FileService&&) -> FileService& = delete;

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

        /**
         * @brief 获取文件列表
         *
         * 业务规则：
         * - 验证 parent_id 文件夹存在且属于用户（如果 parent_id != 0）
         * - 查询 files 和 folders 表，合并结果
         * - 应用 type 过滤（all/file/folder）
         * - 应用排序和分页
         *
         * @param request 文件列表请求
         * @param user_id 用户 ID
         * @return drogon::Task<Result<FileListResponse>> 成功返回文件列表，失败返回错误
         */
        [[nodiscard]]
        auto GetFileList(FileListRequest request, uint64_t user_id)
            -> drogon::Task<Result<FileListResponse>>;

        /**
         * @brief 获取文件详情
         *
         * @param file_id 文件 ID
         * @param user_id 用户 ID
         * @return drogon::Task<Result<FileDetailResponse>> 成功返回文件详情，失败返回错误
         */
        [[nodiscard]]
        auto GetFileDetail(uint64_t file_id, uint64_t user_id)
            -> drogon::Task<Result<FileDetailResponse>>;

        /**
         * @brief 获取下载信息（元数据）
         *
         * 业务规则：
         * - 验证文件存在且属于用户
         * - 关联 file_contents 获取存储信息
         * - 返回文件元数据（不含物理路径）
         *
         * @param file_id 文件 ID
         * @param user_id 用户 ID
         * @return drogon::Task<Result<DownloadInfoResponse>> 成功返回下载信息，失败返回错误
         */
        [[nodiscard]]
        auto GetDownloadInfo(uint64_t file_id, uint64_t user_id)
            -> drogon::Task<Result<DownloadInfoResponse>>;

        /**
         * @brief 获取下载数据（含物理路径）
         *
         * 业务规则：
         * - 验证文件存在且属于用户
         * - 关联 file_contents 获取存储路径
         * - 返回完整下载信息（含 storage_path）
         *
         * @param file_id 文件 ID
         * @param user_id 用户 ID
         * @return drogon::Task<Result<DownloadInfo>> 成功返回下载数据，失败返回错误
         */
        [[nodiscard]]
        auto GetDownloadData(uint64_t file_id, uint64_t user_id)
            -> drogon::Task<Result<DownloadInfo>>;

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

        /**
         * @brief 搜索文件和文件夹
         *
         * 业务规则：
         * - 支持文件名模糊搜索（LIKE %keyword%）
         * - 支持按类型过滤（all/file/folder）
         * - 支持限定搜索范围（folder_id）
         * - 返回结果包含路径面包屑信息
         * - 应用分页
         *
         * @param request 搜索请求
         * @param user_id 用户 ID
         * @return drogon::Task<Result<SearchResponse>> 成功返回搜索结果，失败返回错误
         */
        [[nodiscard]]
        auto Search(SearchRequest request, uint64_t user_id)
            -> drogon::Task<Result<SearchResponse>>;

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

        /**
         * @brief 已有内容元数据
         *
         * 用于秒传/去重场景，缓存已存储文件内容的 ID 和 MIME 类型。
         */
        struct ExistingContentMetadata {
            uint64_t id = 0;
            std::string mime_type;
        };

        /**
         * @brief 已上传分片覆盖信息
         *
         * 用于断点续传场景，描述当前上传任务已完成的分片情况。
         */
        struct UploadedChunkCoverage {
            uint64_t uploaded_count = 0;
            int64_t max_chunk_index = -1;
        };

        static constexpr auto UPLOAD_TASK_CACHE_TTL = std::chrono::seconds(60);
        static constexpr double UPLOAD_TASK_CACHE_MAINTENANCE_INTERVAL_SECONDS = 60.0;

        // ── 原有私有方法（使用 m_db_client） ──

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
        auto FindExistingContent(const std::string& file_hash) const
            -> drogon::Task<std::optional<uint64_t>>;

        [[nodiscard]]
        auto FindExistingTask(uint64_t user_id, const std::string& file_hash) const
            -> drogon::Task<std::optional<drogon_model::disk::UploadTasks>>;

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

        // ── 事务感知辅助方法（接受 DbClientPtr，可传入事务或普通连接） ──

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

        /**
         * @brief 批量递增 file_contents.ref_count（事务版）
         *
         * 在指定连接上执行 CASE WHEN 批量更新。
         *
         * @param client 数据库客户端
         * @param content_ref_increment content_id → 递增量映射
         * @param existing_content_ids 已验证存在的 content_id 集合
         * @return drogon::Task<std::unordered_set<uint64_t>> 实际成功递增的 content_id 集合
         */
        [[nodiscard]]
        auto IncrementContentRefCount(
            const drogon::orm::DbClientPtr& client,
            const std::unordered_map<uint64_t, uint64_t>& content_ref_increment,
            const std::unordered_set<uint64_t>& existing_content_ids
        ) -> drogon::Task<std::unordered_set<uint64_t>>;

        /**
         * @brief 批量插入复制文件记录并获取新 ID（事务版）
         *
         * @param client 数据库客户端
         * @param user_id 用户 ID
         * @param target_folder_id 目标文件夹 ID
         * @param valid_items 待插入的文件列表
         * @return drogon::Task<std::vector<std::pair<uint64_t, uint64_t>>> (old_id, new_id) 映射
         */
        [[nodiscard]]
        auto InsertCopiedFiles(
            const drogon::orm::DbClientPtr& client,
            uint64_t user_id,
            uint64_t target_folder_id,
            const std::vector<std::pair<uint64_t, const drogon_model::disk::Files*>>& valid_items
        ) -> drogon::Task<std::vector<std::pair<uint64_t, uint64_t>>>;

        /**
         * @brief 批量删除文件记录（事务版）
         *
         * @param client 数据库客户端
         * @param file_ids 待删除的文件 ID 列表
         * @return drogon::Task<int> 实际删除的行数
         */
        [[nodiscard]]
        auto DeleteFilesByIds(
            const drogon::orm::DbClientPtr& client,
            const std::vector<uint64_t>& file_ids
        ) -> drogon::Task<int>;

        [[nodiscard]]
        auto LookupExistingContentMetadata(
            const drogon::orm::DbClientPtr& client,
            const std::string& file_hash
        ) const -> drogon::Task<std::optional<ExistingContentMetadata>>;

        [[nodiscard]]
        auto IsFilenameExists(
            const drogon::orm::DbClientPtr& client,
            uint64_t folder_id,
            const std::string& filename,
            uint64_t user_id
        ) const -> drogon::Task<bool>;

        [[nodiscard]]
        auto GetUploadedChunkCoverage(
            const drogon::orm::DbClientPtr& client,
            const std::string& upload_id
        ) const -> drogon::Task<std::optional<UploadedChunkCoverage>>;

        drogon::orm::DbClientPtr m_db_client;                                      ///< 数据库客户端
        storage::IFileStorage* m_storage;                                          ///< 文件存储接口
        std::unordered_map<std::string, UploadTaskCacheEntry> m_upload_task_cache; ///< 上传任务元数据缓存
        std::shared_mutex m_upload_task_cache_mutex;                               ///< 上传任务缓存读写锁
    };

} // namespace disk::file
