/**
 * @file IFileStorage.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件存储边界接口定义
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <drogon/orm/DbClient.h>

#include "utils/ErrorCode.hpp"

namespace disk::storage {

    struct AssembleResult {
        std::filesystem::path path;
        std::string md5_hash;
        std::string sha256_hash;
    };

    struct PromoteResult {
        std::filesystem::path path;
        bool created{ false };
    };

    /**
     * @brief 上传暂存存储边界
     *
     * 职责边界：
     * - 管理上传会话临时目录、分片文件、组装产物和暂存清理
     * - 不负责最终内容 Blob 的读取、删除或持久化路径策略
     * - 不包含 HTTP、数据库和权限校验逻辑
     */
    class UploadStagingStorage {
    public:
        virtual ~UploadStagingStorage() = default;

        /**
         * @brief 确保上传会话的临时目录已创建
         * @param upload_id 上传会话 ID
         * @return 成功返回空，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto EnsureUploadTempDir(const std::string& upload_id)
            -> drogon::Task<Result<void>> = 0;

        /**
         * @brief 写入上传分片到临时目录
         * @param upload_id 上传会话 ID
         * @param chunk_index 分片索引
         * @param data 分片二进制数据
         * @return 成功返回空，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto WriteChunk(
            const std::string& upload_id,
            uint32_t chunk_index,
            std::string data
        ) -> drogon::Task<Result<void>> = 0;

        /**
         * @brief 将 upload_id 对应的全部分片按序组装成临时完整文件
         * @param upload_id 上传会话 ID
         * @param chunk_count 分片总数
         * @return 成功返回组装后的临时文件路径，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto AssembleChunks(const std::string& upload_id, uint32_t chunk_count)
            -> drogon::Task<Result<AssembleResult>> = 0;

        /**
         * @brief 删除指定暂存文件或目录
         * @param target_path 暂存文件或目录路径
         * @return 成功返回空，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto DeleteTempPath(const std::filesystem::path& target_path)
            -> drogon::Task<Result<void>> = 0;

        /**
         * @brief 清理上传会话对应的临时目录及其内容
         * @param upload_id 上传会话 ID
         * @return 成功返回空，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto CleanupTemp(const std::string& upload_id) -> drogon::Task<Result<void>> = 0;
    };

    /**
     * @brief 最终内容 Blob 存储边界
     *
     * 职责边界：
     * - 管理最终内容 Blob 的持久化、读取、存在性检查、删除和路径策略
     * - 不负责上传会话临时分片或暂存目录清理
     * - 不包含 HTTP、数据库和权限校验逻辑
     */
    class BlobStore {
    public:
        virtual ~BlobStore() = default;

        /**
         * @brief 将临时文件移动到最终存储位置（哈希分片目录）
         * @param temp_path 临时文件路径
         * @param hash 文件哈希（如 MD5）
         * @return 成功返回最终存储路径与创建状态，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto PromoteToFinal(const std::filesystem::path& temp_path, const std::string& hash)
            -> drogon::Task<Result<PromoteResult>> = 0;

        /**
         * @brief 打开文件读取句柄用于下载流（支持上层 Range 定位）
         * @param storage_path 存储文件路径
         * @return 成功返回可读文件流，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto OpenForRead(const std::filesystem::path& storage_path)
            -> drogon::Task<Result<std::shared_ptr<std::ifstream>>> = 0;

        /**
         * @brief 安全删除指定最终 Blob 路径
         * @param target_path 目标路径
         * @return 成功返回空，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto DeletePath(const std::filesystem::path& target_path)
            -> drogon::Task<Result<void>> = 0;

        /**
         * @brief 检查最终 Blob 路径是否存在
         * @param target_path 目标路径
         * @return 成功返回存在性布尔值，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto Exists(const std::filesystem::path& target_path) -> drogon::Task<Result<bool>> = 0;

        /**
         * @brief 根据内容哈希计算最终存储路径
         * @param hash 文件内容哈希（如 MD5）
         * @return 最终存储路径
         */
        [[nodiscard]]
        virtual auto GetFinalStoragePath(const std::string& hash) const -> std::filesystem::path = 0;

        /**
         * @brief 获取最终 Blob 文件大小（字节）
         * @param target_path 目标路径
         * @return 成功返回文件大小，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto GetFileSize(const std::filesystem::path& target_path)
            -> drogon::Task<Result<uint64_t>> = 0;
    };

    /**
     * @brief 兼容旧调用点的完整本地文件存储接口
     *
     * 新代码应优先依赖 UploadStagingStorage 或 BlobStore 中的窄边界。
     */
    class IFileStorage : public UploadStagingStorage, public BlobStore {
    public:
        ~IFileStorage() override = default;
    };

} ///< namespace disk::storage
