/**
 * @file LocalBlobStore.hpp
 * @brief 本地最终内容 Blob 存储实现
 * @details 实现 IBlobStore 接口，提供基于本地文件系统的最终 Blob 提升、读取和删除
 * @author LiuFeng (liufeng.code@outlook.com)
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "storage/IBlobStore.hpp"

namespace trantor {
    class ConcurrentTaskQueue;
}

namespace disk::utils {
    class ConfigMgr;
}

namespace disk::storage {

    /**
     * @brief 本地最终内容 Blob 存储实现类
     *
     * 职责边界：
     * - 实现基于内容哈希的最终 Blob 路径计算
     * - 负责最终 Blob 的提升、读取和删除
     * - 不处理上传分片、组装或临时目录生命周期
     */
    class LocalBlobStore : public IBlobStore {
    public:
        /**
         * @brief 构造本地 Blob 存储实例
         * @param config_mgr 配置管理器（可选，用于获取最终存储根目录）
         */
        explicit LocalBlobStore(std::shared_ptr<disk::utils::ConfigMgr> config_mgr = nullptr);
        ~LocalBlobStore() override = default;
        LocalBlobStore(const LocalBlobStore&) = delete;
        auto operator=(const LocalBlobStore&) -> LocalBlobStore& = delete;
        LocalBlobStore(LocalBlobStore&&) = default;
        auto operator=(LocalBlobStore&&) -> LocalBlobStore& = default;

        [[nodiscard]]
        auto PromoteToFinal(
            const UploadStagingAssembly& assembly,
            const std::string& sha256_hash,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<BlobPromoteResult>> override;

        [[nodiscard]]
        auto OpenBlobRangeForRead(
            const BlobDescriptor& blob,
            uint64_t start,
            uint64_t length,
            disk::utils::LogContext log_context = {}
        ) -> drogon::Task<Result<std::shared_ptr<StorageReadStream>>> override;

        [[nodiscard]]
        auto DeleteBlob(
            const std::filesystem::path& storage_path,
            disk::utils::LogContext log_context = {}
        ) -> drogon::Task<Result<void>> override;

        [[nodiscard]]
        auto BlobExists(
            const BlobDescriptor& blob,
            disk::utils::LogContext log_context = {}
        ) -> drogon::Task<Result<bool>> override;

        [[nodiscard]]
        auto GetLocalBlobPathForDownload(const BlobDescriptor& blob) const
            -> std::optional<std::filesystem::path> override;

        [[nodiscard]]
        auto GetBlobSize(
            const BlobDescriptor& blob,
            disk::utils::LogContext log_context = {}
        ) -> drogon::Task<Result<uint64_t>> override;

        [[nodiscard]]
        auto ListFinalObjects(
            const std::string& continuation_token,
            size_t limit,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<StorageInventoryPage>> override;

    private:
        std::shared_ptr<disk::utils::ConfigMgr> m_config_mgr{};         ///< 配置管理器
        std::shared_ptr<trantor::ConcurrentTaskQueue> m_worker_queue{}; ///< 最终 Blob 文件系统工作队列
    };

} // namespace disk::storage
