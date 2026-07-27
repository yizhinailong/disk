/**
 * @file IBlobStore.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 最终内容 Blob 存储边界接口定义
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>

#include "storage/BlobDescriptor.hpp"
#include "storage/StorageInventory.hpp"
#include "storage/UploadStagingStorage.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/LogHelper.hpp"

namespace disk::storage {

    struct BlobPromoteResult {
        std::filesystem::path path;
        bool created{ false };
    };

    class StorageReadStream {
    public:
        virtual ~StorageReadStream() = default;

        [[nodiscard]]
        virtual auto Read(char* buffer, std::size_t length) -> std::size_t = 0;

        virtual auto Close() -> void = 0;
    };

    /**
     * @brief 最终内容 Blob 存储抽象接口
     *
     * 职责边界：
     * - 仅处理去重后的最终内容 Blob
     * - 不包含上传分片、组装和临时目录管理
     * - 不包含 HTTP、数据库、权限和引用计数校验逻辑
     */
    class IBlobStore {
    public:
        virtual ~IBlobStore() = default;

        /**
         * @brief 将已组装暂存对象提升为最终 Blob
         * @param assembly 后端无关的组装暂存对象描述符
         * @param sha256_hash 64 位小写十六进制 SHA-256 内容哈希
         * @return 成功返回最终路径与是否由本次调用创建，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto PromoteToFinal(
            const UploadStagingAssembly& assembly,
            const std::string& sha256_hash,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<BlobPromoteResult>> = 0;

        /**
         * @brief 通过最终 Blob 描述符打开限定范围的读取流
         * @param blob 最终内容 Blob 描述符
         * @param start 起始字节偏移
         * @param length 读取字节数
         * @return 成功返回可读流，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto OpenBlobRangeForRead(
            const BlobDescriptor& blob,
            uint64_t start,
            uint64_t length,
            disk::utils::LogContext log_context = {}
        ) -> drogon::Task<Result<std::shared_ptr<StorageReadStream>>> = 0;

        /**
         * @brief 通过最终 Blob 描述符检查内容是否存在
         * @param blob 最终内容 Blob 描述符
         * @return 成功返回存在性布尔值，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto BlobExists(
            const BlobDescriptor& blob,
            disk::utils::LogContext log_context = {}
        ) -> drogon::Task<Result<bool>> = 0;

        /**
         * @brief 获取可交给本地文件响应使用的路径（非本地存储可不支持）
         * @param blob 最终内容 Blob 描述符
         * @return 本地可发送路径；不支持时返回 std::nullopt
         */
        [[nodiscard]]
        virtual auto GetLocalBlobPathForDownload(const BlobDescriptor& blob) const
            -> std::optional<std::filesystem::path> {
            static_cast<void>(blob);
            return std::nullopt;
        }

        /**
         * @brief 删除最终 Blob；缺失路径视为成功
         * @param storage_path 最终 Blob 存储路径
         * @return 成功返回空，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto DeleteBlob(
            const std::filesystem::path& storage_path,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<void>> = 0;

        /**
         * @brief 通过持久化描述符获取最终 Blob 实际大小（字节）
         * @param blob 最终内容 Blob 描述符
         * @return 成功返回后端实际大小，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto GetBlobSize(
            const BlobDescriptor& blob,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<uint64_t>> = 0;

        /**
         * @brief 分页列举当前后端配置的全部 final Blob
         */
        [[nodiscard]]
        virtual auto ListFinalObjects(
            const std::string& continuation_token,
            size_t limit,
            disk::utils::LogContext log_context = {}
        ) -> drogon::Task<Result<StorageInventoryPage>> {
            static_cast<void>(continuation_token);
            static_cast<void>(limit);
            static_cast<void>(log_context);
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Final Blob inventory is not supported")
            );
        }
    };

} // namespace disk::storage
