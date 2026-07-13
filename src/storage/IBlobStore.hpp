/**
 * @file IBlobStore.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 最终内容 Blob 存储边界接口定义
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>

#include "storage/BlobDescriptor.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::storage {

    struct BlobPromoteResult {
        std::filesystem::path path;
        bool created{ false };
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
         * @brief 将已组装临时文件提升为最终 Blob
         * @param temp_path 已组装临时文件路径
         * @param hash 内容哈希（如 MD5）
         * @return 成功返回最终路径与是否由本次调用创建，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto PromoteToFinal(const std::filesystem::path& temp_path, const std::string& hash)
            -> drogon::Task<Result<BlobPromoteResult>> = 0;

        /**
         * @brief 打开最终 Blob 读取句柄用于下载流
         * @param storage_path 最终 Blob 存储路径
         * @return 成功返回可读文件流，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto OpenForRead(const std::filesystem::path& storage_path)
            -> drogon::Task<Result<std::shared_ptr<std::ifstream>>> = 0;

        /**
         * @brief 通过最终 Blob 描述符打开读取句柄用于下载流
         * @param blob 最终内容 Blob 描述符
         * @return 成功返回可读文件流，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto OpenBlobForRead(const BlobDescriptor& blob)
            -> drogon::Task<Result<std::shared_ptr<std::ifstream>>> {
            co_return co_await OpenForRead(GetFinalStoragePath(blob.hash_md5));
        }

        /**
         * @brief 通过最终 Blob 描述符检查内容是否存在
         * @param blob 最终内容 Blob 描述符
         * @return 成功返回存在性布尔值，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto BlobExists(const BlobDescriptor& blob) -> drogon::Task<Result<bool>> {
            co_return co_await Exists(GetFinalStoragePath(blob.hash_md5));
        }

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
        virtual auto DeleteBlob(const std::filesystem::path& storage_path)
            -> drogon::Task<Result<void>> = 0;

        /**
         * @brief 检查最终 Blob 是否存在
         * @param storage_path 最终 Blob 存储路径
         * @return 成功返回存在性布尔值，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto Exists(const std::filesystem::path& storage_path) -> drogon::Task<Result<bool>> = 0;

        /**
         * @brief 根据内容哈希计算最终 Blob 存储路径
         * @param hash 文件内容哈希（如 MD5）
         * @return 最终 Blob 存储路径
         */
        [[nodiscard]]
        virtual auto GetFinalStoragePath(const std::string& hash) const -> std::filesystem::path = 0;

        /**
         * @brief 获取最终 Blob 大小（字节）
         * @param storage_path 最终 Blob 存储路径
         * @return 成功返回文件大小，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto GetFileSize(const std::filesystem::path& storage_path)
            -> drogon::Task<Result<uint64_t>> = 0;
    };

} ///< namespace disk::storage
