/**
 * @file FileService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件上传服务
 * @version 0.1
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>

#include <drogon/orm/DbClient.h>

#include "dtos/FileDto.hpp"
#include "models/UploadTasks.hpp"
#include "utils/ErrorCode.hpp"

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
    class FileService {
    public:
        /**
         * @brief 构造函数
         * @param db_client 数据库客户端
         */
        explicit FileService(drogon::orm::DbClientPtr db_client);
        ~FileService() = default;
        FileService(const FileService&) = delete;
        auto operator=(const FileService&) -> FileService& = delete;
        FileService(FileService&&) = default;
        auto operator=(FileService&&) -> FileService& = default;

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
            std::string chunk_data,
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
         * @brief 检查存储配额
         *
         * @param user_id 用户 ID
         * @param file_size 文件大小
         * @return drogon::Task<Result<void>> 成功返回空，失败返回错误
         */
        [[nodiscard]]
        auto CheckStorageQuota(uint64_t user_id, uint64_t file_size) const
            -> drogon::Task<Result<void>>;

        /**
         * @brief 查找已存在的文件内容（用于秒传）
         *
         * @param file_hash 文件 MD5 哈希
         * @return drogon::Task<std::optional<uint64_t>> 存在返回 content_id，不存在返回空
         */
        [[nodiscard]]
        auto FindExistingContent(const std::string& file_hash) const
            -> drogon::Task<std::optional<uint64_t>>;

        /**
         * @brief 查找已存在的上传任务（用于断点续传）
         *
         * @param user_id 用户 ID
         * @param file_hash 文件 MD5 哈希
         * @return drogon::Task<std::optional<drogon_model::disk::UploadTasks>> 存在返回上传任务，不存在返回空
         */
        [[nodiscard]]
        auto FindExistingTask(uint64_t user_id, const std::string& file_hash) const
            -> drogon::Task<std::optional<drogon_model::disk::UploadTasks>>;

        /**
         * @brief 查找上传任务
         *
         * @param upload_id 上传会话 ID
         * @param user_id 用户 ID
         * @return drogon::Task<Result<drogon_model::disk::UploadTasks>> 成功返回上传任务，失败返回错误
         */
        [[nodiscard]]
        auto FindUploadTask(const std::string& upload_id, uint64_t user_id) const
            -> drogon::Task<Result<drogon_model::disk::UploadTasks>>;

        /**
         * @brief 更新用户存储使用量
         *
         * @param user_id 用户 ID
         * @param delta 变化量（正数为增加，负数为减少）
         * @return drogon::Task<void>
         */
        auto UpdateStorageUsed(uint64_t user_id, int64_t delta) -> drogon::Task<void>;

        /**
         * @brief 解析已上传分片列表
         *
         * @param uploaded_chunks_json JSON 字符串
         * @return std::set<uint32_t> 已上传分片索引集合
         */
        [[nodiscard]]
        static auto ParseUploadedChunks(const std::string& uploaded_chunks_json) -> std::set<uint32_t>;

        /**
         * @brief 序列化已上传分片列表为 JSON
         *
         * @param chunks 已上传分片索引集合
         * @return std::string JSON 字符串
         */
        [[nodiscard]]
        static auto SerializeUploadedChunks(const std::set<uint32_t>& chunks) -> std::string;

        /**
         * @brief 获取分片临时文件路径
         *
         * @param upload_id 上传会话 ID
         * @param chunk_index 分片索引
         * @return std::filesystem::path 分片文件路径
         */
        [[nodiscard]]
        auto GetChunkFilePath(const std::string& upload_id, uint32_t chunk_index) const
            -> std::filesystem::path;

        /**
         * @brief 获取临时目录路径
         *
         * @param upload_id 上传会话 ID
         * @return std::filesystem::path 临时目录路径
         */
        [[nodiscard]]
        auto GetTempDirPath(const std::string& upload_id) const -> std::filesystem::path;

        /**
         * @brief 获取组装临时文件路径
         *
         * @param upload_id 上传会话 ID
         * @return std::filesystem::path 组装文件路径
         */
        [[nodiscard]]
        auto GetAssembleFilePath(const std::string& upload_id) const -> std::filesystem::path;

        /**
         * @brief 获取最终存储路径
         *
         * @param file_hash 文件 MD5 哈希
         * @return std::filesystem::path 最终存储路径
         */
        [[nodiscard]]
        auto GetFinalStoragePath(const std::string& file_hash) const -> std::filesystem::path;

        /**
         * @brief 检查文件夹中是否存在同名文件
         *
         * @param folder_id 文件夹 ID
         * @param filename 文件名
         * @param user_id 用户 ID
         * @return drogon::Task<bool> 存在返回 true
         */
        [[nodiscard]]
        auto IsFilenameExists(uint64_t folder_id, const std::string& filename, uint64_t user_id) const
            -> drogon::Task<bool>;

        /**
         * @brief 从文件名提取扩展名
         *
         * @param filename 文件名
         * @return std::string 扩展名（不含点）
         */
        [[nodiscard]]
        static auto ExtractExtension(const std::string& filename) -> std::string;

        /**
         * @brief 根据 MIME 类型判断是否为图片
         *
         * @param mime_type MIME 类型
         * @return bool 是否为图片
         */
        [[nodiscard]]
        static auto IsImageMimeType(const std::string& mime_type) -> bool;

        drogon::orm::DbClientPtr m_db_client; ///< 数据库客户端
    };

} // namespace disk::file
