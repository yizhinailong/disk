/**
 * @file IFileStorage.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 上传暂存文件存储边界接口定义
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include <drogon/orm/DbClient.h>

#include "utils/ErrorCode.hpp"

namespace disk::storage {

    struct AssembleResult {
        std::filesystem::path path;
        std::string md5_hash;
        std::string sha256_hash;
    };

    /**
     * @brief 上传暂存文件存储抽象接口
     *
     * 职责边界：
     * - 仅处理上传会话、分片、组装文件和临时清理
     * - 不包含最终内容 Blob 的提升、读取和删除
     * - 不包含 HTTP、数据库和权限校验逻辑
     * - 使用 Result<T> 作为统一错误契约
     */
    class IFileStorage {
    public:
        virtual ~IFileStorage() = default;

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
         * @brief 安全删除上传暂存文件或目录
         * @param target_path 目标路径
         * @return 成功返回空，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto DeletePath(const std::filesystem::path& target_path)
            -> drogon::Task<Result<void>> = 0;

        /**
         * @brief 清理上传会话对应的临时目录及其内容
         * @param upload_id 上传会话 ID
         * @return 成功返回空，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto CleanupTemp(const std::string& upload_id) -> drogon::Task<Result<void>> = 0;
    };

} ///< namespace disk::storage
