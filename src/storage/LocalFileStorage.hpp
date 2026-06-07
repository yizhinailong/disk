/**
 * @file LocalFileStorage.hpp
 * @brief 本地文件存储实现
 * @details 实现 IFileStorage 接口，提供基于本地文件系统的分片上传、文件组装、
 *          临时文件管理和最终存储功能
 * @author LiuFeng (liufeng.code@outlook.com)
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

#include "storage/IFileStorage.hpp"

namespace trantor {
    class ConcurrentTaskQueue;
}

namespace disk::utils {
    class ConfigMgr;
}

namespace disk::storage {

    /**
     * @brief 本地文件存储实现类
     *
     * 职责边界：
     * - 实现 IFileStorage 接口的所有文件系统操作
     * - 管理上传会话的临时目录和分片文件
     * - 实现基于内容哈希的最终存储路径计算
     * - 使用 Result<T> 作为统一错误契约
     */
    class LocalFileStorage : public IFileStorage {
    public:
        /**
         * @brief 构造本地文件存储实例
         * @param config_mgr 配置管理器（可选，用于获取存储根目录）
         */
        explicit LocalFileStorage(std::shared_ptr<disk::utils::ConfigMgr> config_mgr = nullptr);
        ~LocalFileStorage() override = default;
        LocalFileStorage(const LocalFileStorage&) = delete;
        auto operator=(const LocalFileStorage&) -> LocalFileStorage& = delete;
        LocalFileStorage(LocalFileStorage&&) = default;
        auto operator=(LocalFileStorage&&) -> LocalFileStorage& = default;

        /**
         * @brief 确保上传会话的临时目录已创建
         * @param upload_id 上传会话 ID
         * @return 成功返回空，失败返回错误信息
         */
        [[nodiscard]]
        auto EnsureUploadTempDir(const std::string& upload_id)
            -> drogon::Task<Result<void>> override;

        /**
         * @brief 写入上传分片到临时目录
         * @param upload_id 上传会话 ID
         * @param chunk_index 分片索引
         * @param data 分片二进制数据
         * @return 成功返回空，失败返回错误信息
         */
        [[nodiscard]]
        auto WriteChunk(const std::string& upload_id, uint32_t chunk_index, std::string data)
            -> drogon::Task<Result<void>> override;

        /**
         * @brief 将上传会话对应的全部分片按序组装成临时完整文件
         * @param upload_id 上传会话 ID
         * @param chunk_count 分片总数
         * @return 成功返回组装后的临时文件路径，失败返回错误信息
         */
        [[nodiscard]]
        auto AssembleChunks(const std::string& upload_id, uint32_t chunk_count)
            -> drogon::Task<Result<AssembleResult>> override;

        /**
         * @brief 将临时文件移动到最终存储位置（哈希分片目录）
         * @param temp_path 临时文件路径
         * @param hash 文件哈希（如 MD5）
         * @return 成功返回最终存储路径，失败返回错误信息
         */
        [[nodiscard]]
        auto PromoteToFinal(const std::filesystem::path& temp_path, const std::string& hash)
            -> drogon::Task<Result<std::filesystem::path>> override;

        /**
         * @brief 打开文件读取句柄用于下载流（支持上层范围定位）
         * @param storage_path 存储文件路径
         * @return 成功返回可读文件流，失败返回错误信息
         */
        [[nodiscard]]
        auto OpenForRead(const std::filesystem::path& storage_path)
            -> drogon::Task<Result<std::shared_ptr<std::ifstream>>> override;

        /**
         * @brief 安全删除指定文件或目录
         * @param target_path 目标路径
         * @return 成功返回空，失败返回错误信息
         */
        [[nodiscard]]
        auto DeletePath(const std::filesystem::path& target_path) -> drogon::Task<Result<void>> override;

        /**
         * @brief 清理上传会话对应的临时目录及其内容
         * @param upload_id 上传会话 ID
         * @return 成功返回空，失败返回错误信息
         */
        [[nodiscard]]
        auto CleanupTemp(const std::string& upload_id) -> drogon::Task<Result<void>> override;

        /**
         * @brief 检查路径是否存在
         * @param target_path 目标路径
         * @return 成功返回存在性布尔值，失败返回错误信息
         */
        [[nodiscard]]
        auto Exists(const std::filesystem::path& target_path) -> drogon::Task<Result<bool>> override;

        /**
         * @brief 根据内容哈希计算最终存储路径
         * @param hash 文件内容哈希（如 MD5）
         * @return 最终存储路径
         */
        [[nodiscard]]
        auto GetFinalStoragePath(const std::string& hash) const -> std::filesystem::path override;

        /**
         * @brief 获取文件大小（字节）
         * @param target_path 目标路径
         * @return 成功返回文件大小，失败返回错误信息
         */
        [[nodiscard]]
        auto GetFileSize(const std::filesystem::path& target_path) -> drogon::Task<Result<uint64_t>> override;

    private:
        /**
         * @brief 获取上传会话的临时目录路径
         * @param upload_id 上传会话 ID
         * @return 临时目录路径
         */
        [[nodiscard]]
        auto GetTempDirPath(const std::string& upload_id) const -> std::filesystem::path;

        /**
         * @brief 获取分片文件的完整路径
         * @param upload_id 上传会话 ID
         * @param chunk_index 分片索引
         * @return 分片文件路径
         */
        [[nodiscard]]
        auto GetChunkFilePath(const std::string& upload_id, uint32_t chunk_index) const
            -> std::filesystem::path;

        /**
         * @brief 获取组装文件的临时路径
         * @param upload_id 上传会话 ID
         * @return 组装文件路径
         */
        [[nodiscard]]
        auto GetAssembleFilePath(const std::string& upload_id) const -> std::filesystem::path;

        std::shared_ptr<disk::utils::ConfigMgr> m_config_mgr{};                  ///< 配置管理器
        std::shared_ptr<trantor::ConcurrentTaskQueue> m_worker_queue{};          ///< 短时文件系统阻塞操作工作队列
        std::shared_ptr<trantor::ConcurrentTaskQueue> m_assembly_worker_queue{}; ///< 分片组装专用工作队列
    };

} // namespace disk::storage
