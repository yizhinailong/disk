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

#include "storage/StorageInventory.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/LogHelper.hpp"

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

    struct UploadStagingObjectHead {
        bool exists{ false };
        std::optional<uint64_t> size_bytes;
        std::optional<std::string> etag;
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
         * @brief 确保上传会话已准备好
         * @param session 持久化上传会话描述符
         * @return 成功返回空，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto EnsureUploadSession(
            const UploadStagingSession& session,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<void>> = 0;

        /**
         * @brief 写入上传分片到暂存目录
         * @param session 持久化上传会话描述符
         * @param chunk_index 分片索引
         * @param md5_hash 已由服务端验证的分片 MD5
         * @param data 分片二进制数据
         * @return 成功返回不可变分片对象描述符，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto WriteChunk(
            const UploadStagingSession& session,
            uint32_t chunk_index,
            const std::string& md5_hash,
            std::string data,
            disk::utils::LogContext log_context = {}
        ) -> drogon::Task<Result<UploadStagingChunk>> = 0;

        /**
         * @brief 读取权威分片描述符指向的对象元数据
         * @param session 持久化上传会话描述符
         * @param chunk PostgreSQL 中的权威分片描述符
         * @return 存在性、大小和可用的 ETag，不读取对象正文
         */
        [[nodiscard]]
        virtual auto HeadChunkObject(
            const UploadStagingSession& session,
            const UploadStagingChunk& chunk,
            disk::utils::LogContext log_context = {}
        ) -> drogon::Task<Result<UploadStagingObjectHead>> {
            static_cast<void>(session);
            static_cast<void>(chunk);
            static_cast<void>(log_context);
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Staging object HEAD is not supported")
            );
        }

        /**
         * @brief 将会话对应的全部分片按序组装成暂存完整文件
         * @param session 持久化上传会话描述符
         * @param state_version 完成租约状态版本
         * @param expected_chunk_count 上传任务声明的分片总数
         * @param chunks PostgreSQL 按索引返回的权威分片描述符
         * @return 成功返回暂存组装结果，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto AssembleChunks(
            const UploadStagingSession& session,
            uint64_t state_version,
            uint32_t expected_chunk_count,
            const std::vector<UploadStagingChunk>& chunks,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<UploadStagingAssembly>> = 0;

        /**
         * @brief 丢弃指定上传会话的组装暂存工件
         * @param session 持久化上传会话描述符
         * @param assembly 组装暂存工件描述
         * @return 成功返回空，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto DiscardAssembly(
            const UploadStagingSession& session,
            const UploadStagingAssembly& assembly,
            disk::utils::LogContext log_context = {}
        ) -> drogon::Task<Result<void>> = 0;

        /**
         * @brief 清理上传会话对应的暂存工件
         * @param session 持久化上传会话描述符
         * @return 成功返回空，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto CleanupSession(
            const UploadStagingSession& session,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<void>> = 0;

        /**
         * @brief 分页列举当前后端配置的全部 staging 对象
         */
        [[nodiscard]]
        virtual auto ListStagingObjects(
            const std::string& continuation_token,
            size_t limit,
            disk::utils::LogContext log_context = {}
        ) -> drogon::Task<Result<StorageInventoryPage>> {
            static_cast<void>(continuation_token);
            static_cast<void>(limit);
            static_cast<void>(log_context);
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Staging inventory is not supported")
            );
        }
    };

} // namespace disk::storage
