/**
 * @file UploadStagingStorage.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 上传暂存存储边界接口定义
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <drogon/orm/DbClient.h>

#include "utils/ErrorCode.hpp"

namespace disk::storage {

    struct UploadStagingAssembly {
        std::filesystem::path path;
        std::string md5_hash;
        std::string sha256_hash;
    };

    /**
     * @brief 上传暂存存储抽象接口
     *
     * 职责边界：
     * - 管理上传会话临时目录和分片文件
     * - 组装上传分片为暂存完整文件
     * - 清理上传暂存工件
     * - 不包含最终 blob 提升、下载读取和最终存储删除语义
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
         * @brief 写入上传分片到暂存目录
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
         * @brief 将 upload_id 对应的全部分片按序组装成暂存完整文件
         * @param upload_id 上传会话 ID
         * @param chunk_count 分片总数
         * @return 成功返回暂存组装结果，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto AssembleChunks(const std::string& upload_id, uint32_t chunk_count)
            -> drogon::Task<Result<UploadStagingAssembly>> = 0;

        /**
         * @brief 丢弃指定上传会话的组装暂存工件
         * @param upload_id 上传会话 ID
         * @param assembly 组装暂存工件描述
         * @return 成功返回空，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto DiscardAssembly(
            const std::string& upload_id,
            const UploadStagingAssembly& assembly
        ) -> drogon::Task<Result<void>> = 0;

        /**
         * @brief 清理上传会话对应的暂存目录及组装工件
         * @param upload_id 上传会话 ID
         * @return 成功返回空，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto CleanupTemp(const std::string& upload_id) -> drogon::Task<Result<void>> = 0;
    };

} ///< namespace disk::storage
