/**
 * @file IBlobStorage.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 内容 Blob 存储边界接口定义
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

    struct PromoteResult {
        std::filesystem::path path;
        bool created{ false };
    };

    /**
     * @brief 内容 Blob 存储抽象接口
     *
     * 职责边界：
     * - 仅处理最终 content blob 的提升、读取、删除和路径计算
     * - 不处理上传 session 的临时分片和暂存清理
     * - 使用 Result<T> 作为统一错误契约
     */
    class IBlobStorage {
    public:
        virtual ~IBlobStorage() = default;

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
         * @brief 删除最终 Blob 文件
         * @param blob_path Blob 路径
         * @return 成功返回空，失败返回错误信息
         */
        [[nodiscard]]
        virtual auto DeleteBlob(const std::filesystem::path& blob_path)
            -> drogon::Task<Result<void>> = 0;

        /**
         * @brief 检查 Blob 路径是否存在
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
