/**
 * @file LocalFileStorage.hpp
 * @brief 本地上传暂存文件存储实现
 * @details 提供基于本地文件系统的分片上传、文件组装和临时文件管理
 * @author LiuFeng (liufeng.code@outlook.com)
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "storage/IFileStorage.hpp"
#include "storage/UploadStagingStorage.hpp"

namespace trantor {
    class ConcurrentTaskQueue;
}

namespace disk::utils {
    class ConfigMgr;
}

namespace disk::storage {

    /**
     * @brief 本地上传暂存文件存储实现类
     *
     * 职责边界：
     * - 管理上传会话的临时目录和分片文件
     * - 组装分片并计算内容哈希
     * - 清理上传暂存文件
     * - 不处理最终内容 Blob 的提升、读取和删除
     */
    class LocalFileStorage : public IFileStorage, public UploadStagingStorage {
    public:
        /**
         * @brief 构造本地文件存储实例
         * @param config_mgr 配置管理器（可选，用于获取暂存根目录）
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
        auto EnsureUploadSession(const UploadStagingSession& session)
            -> drogon::Task<Result<void>> override;

        /**
         * @brief 写入上传分片到临时目录
         * @param upload_id 上传会话 ID
         * @param chunk_index 分片索引
         * @param data 分片二进制数据
         * @return 成功返回空，失败返回错误信息
         */
        [[nodiscard]]
        auto WriteChunk(
            const UploadStagingSession& session,
            uint32_t chunk_index,
            const std::string& md5_hash,
            std::string data
        ) -> drogon::Task<Result<UploadStagingChunk>> override;

        /**
         * @brief 将上传会话对应的全部分片按序组装成临时完整文件
         * @param upload_id 上传会话 ID
         * @param expected_chunk_count 上传任务声明的分片总数
         * @param chunks PostgreSQL 按索引返回的权威分片描述符
         * @return 成功返回组装后的临时文件路径，失败返回错误信息
         */
        [[nodiscard]]
        auto AssembleChunks(
            const UploadStagingSession& session,
            uint64_t state_version,
            uint32_t expected_chunk_count,
            const std::vector<UploadStagingChunk>& chunks
        )
            -> drogon::Task<Result<UploadStagingAssembly>> override;

        /**
         * @brief 丢弃上传会话对应的组装暂存工件
         * @param upload_id 上传会话 ID
         * @param assembly 组装暂存工件描述
         * @return 成功返回空，失败返回错误信息
         */
        [[nodiscard]]
        auto DiscardAssembly(
            const UploadStagingSession& session,
            const UploadStagingAssembly& assembly
        )
            -> drogon::Task<Result<void>> override;

        /**
         * @brief 安全删除上传暂存文件或目录
         * @param target_path 目标路径
         * @return 成功返回空，失败返回错误信息
         */
        [[nodiscard]]
        auto DeletePath(const std::filesystem::path& target_path) -> drogon::Task<Result<void>>;

        /**
         * @brief 清理上传会话对应的临时目录及其内容
         * @param upload_id 上传会话 ID
         * @return 成功返回空，失败返回错误信息
         */
        [[nodiscard]]
        auto CleanupSession(const UploadStagingSession& session)
            -> drogon::Task<Result<void>> override;

        [[nodiscard]]
        auto ListStagingObjects(const std::string& continuation_token, size_t limit)
            -> drogon::Task<Result<StorageInventoryPage>> override;

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
         * @param md5_hash 分片 MD5
         * @return 分片对象 key
         */
        [[nodiscard]]
        static auto GetChunkObjectKey(
            const std::string& upload_id,
            uint32_t chunk_index,
            const std::string& md5_hash
        ) -> std::string;

        [[nodiscard]]
        auto ResolveChunkFilePath(
            const std::string& upload_id,
            const UploadStagingChunk& chunk
        ) const -> Result<std::filesystem::path>;

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
