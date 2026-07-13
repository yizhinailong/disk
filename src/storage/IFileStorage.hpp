/**
 * @file IFileStorage.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件存储边界接口定义
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#include <drogon/orm/DbClient.h>

#include "utils/ErrorCode.hpp"

namespace disk::storage {

    struct PromoteResult {
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

    class FileStorageReadStream final : public StorageReadStream {
    public:
        FileStorageReadStream(std::shared_ptr<std::ifstream> stream, uint64_t remaining)
            : m_stream(std::move(stream)), m_remaining(remaining) {}

        [[nodiscard]]
        auto Read(char* buffer, std::size_t length) -> std::size_t override {
            if (buffer == nullptr || m_stream == nullptr || !m_stream->is_open() || m_remaining == 0) {
                return 0;
            }

            const auto bounded_remaining = static_cast<std::size_t>(std::min<uint64_t>(
                m_remaining,
                static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())
            ));
            const auto read_size = std::min(length, bounded_remaining);
            m_stream->read(buffer, static_cast<std::streamsize>(read_size));
            const auto read_bytes = static_cast<std::size_t>(m_stream->gcount());
            if (read_bytes == 0) {
                m_remaining = 0;
                Close();
                return 0;
            }

            m_remaining -= read_bytes;
            if (m_remaining == 0) {
                Close();
            }
            return read_bytes;
        }

        auto Close() -> void override {
            if (m_stream != nullptr && m_stream->is_open()) {
                m_stream->close();
            }
        }

    private:
        std::shared_ptr<std::ifstream> m_stream;
        uint64_t m_remaining{ 0 };
    };

    /**
     * @brief 文件存储抽象接口
     *
     * 职责边界：
     * - 仅处理文件系统相关操作
     * - 不包含 HTTP、数据库和权限校验逻辑
     * - 使用 Result<T> 作为统一错误契约
     */
    class IFileStorage {
    public:
        virtual ~IFileStorage() = default;

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
         * @brief 当前后端是否支持直接文件响应（如 sendfile）
         * @return 本地文件路径后端返回 true，对象存储等远程后端返回 false
         */
        [[nodiscard]]
        virtual auto SupportsDirectFileResponse() const noexcept -> bool {
            return false;
        }

        /**
         * @brief 打开限定范围的读取流，用于 backend-neutral 下载响应
         * @param storage_path 存储文件路径或对象 key
         * @param start 起始字节偏移
         * @param length 读取字节数
         * @return 成功返回可读流，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto OpenForReadRange(
            const std::filesystem::path& storage_path,
            uint64_t start,
            uint64_t length
        ) -> drogon::Task<Result<std::shared_ptr<StorageReadStream>>> {
            auto open_result = co_await OpenForRead(storage_path);
            if (!open_result) {
                co_return std::unexpected(open_result.error());
            }

            if (start > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max())) {
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Invalid request range")
                );
            }

            auto stream = std::move(*open_result);
            stream->seekg(static_cast<std::streamoff>(start));
            if (!*stream) {
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::FileReadError, "Failed to seek file for reading")
                );
            }

            co_return std::shared_ptr<StorageReadStream>(
                std::make_shared<FileStorageReadStream>(std::move(stream), length)
            );
        }

        /**
         * @brief 安全删除指定文件或目录
         * @param target_path 目标路径
         * @return 成功返回空，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto DeletePath(const std::filesystem::path& target_path)
            -> drogon::Task<Result<void>> = 0;

        /**
         * @brief 检查路径是否存在
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
         * @brief 获取文件大小（字节）
         * @param target_path 目标路径
         * @return 成功返回文件大小，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto GetFileSize(const std::filesystem::path& target_path)
            -> drogon::Task<Result<uint64_t>> = 0;
    };

} ///< namespace disk::storage
