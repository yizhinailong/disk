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
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "utils/ErrorCode.hpp"

namespace disk::storage {

    enum class UploadStagingBackend {
        Local,
        S3,
    };

    [[nodiscard]] constexpr auto ToStorageValue(UploadStagingBackend backend) noexcept
        -> std::string_view {
        switch (backend) {
            case UploadStagingBackend::Local:
                return "local";
            case UploadStagingBackend::S3:
                return "s3";
        }
        return "local";
    }

    [[nodiscard]] constexpr auto ParseUploadStagingBackend(std::string_view value) noexcept
        -> std::optional<UploadStagingBackend> {
        if (value == "local") {
            return UploadStagingBackend::Local;
        }
        if (value == "s3") {
            return UploadStagingBackend::S3;
        }
        return std::nullopt;
    }

    struct UploadStagingSession {
        std::string upload_id;
        UploadStagingBackend backend{ UploadStagingBackend::Local };
        std::string prefix;
    };

    struct UploadStagingChunk {
        uint32_t chunk_index{ 0 };
        uint64_t size_bytes{ 0 };
        std::string md5_hash;
        std::string object_key;
        std::string etag;
    };

    struct UploadStagingAssembly {
        UploadStagingBackend backend{ UploadStagingBackend::Local };
        std::string locator;
        uint64_t size_bytes{ 0 };
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
         * @param md5_hash 已由服务端验证的分片 MD5
         * @param data 分片二进制数据
         * @return 成功返回不可变分片对象描述符，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto WriteChunk(
            const std::string& upload_id,
            uint32_t chunk_index,
            const std::string& md5_hash,
            std::string data
        ) -> drogon::Task<Result<UploadStagingChunk>> = 0;

        /**
         * @brief 将 upload_id 对应的全部分片按序组装成暂存完整文件
         * @param upload_id 上传会话 ID
         * @param expected_chunk_count 上传任务声明的分片总数
         * @param chunks PostgreSQL 按索引返回的权威分片描述符
         * @return 成功返回暂存组装结果，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto AssembleChunks(
            const std::string& upload_id,
            uint32_t expected_chunk_count,
            const std::vector<UploadStagingChunk>& chunks
        )
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

} // namespace disk::storage
