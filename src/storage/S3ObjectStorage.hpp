#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "storage/IBlobStore.hpp"
#include "storage/IFileStorage.hpp"
#include "storage/LocalFileStorage.hpp"
#include "storage/S3Client.hpp"
#include "storage/UploadStagingStorage.hpp"
#include "utils/ConfigMgr.hpp"

namespace trantor {
    class ConcurrentTaskQueue;
}

namespace disk::storage {

    class S3ObjectStorage final : public IFileStorage, public IBlobStore, public UploadStagingStorage {
    public:
        S3ObjectStorage(
            std::shared_ptr<disk::utils::ConfigMgr> config_mgr,
            std::shared_ptr<IS3Client> s3_client
        );
        ~S3ObjectStorage() override = default;

        S3ObjectStorage(const S3ObjectStorage&) = delete;
        auto operator=(const S3ObjectStorage&) -> S3ObjectStorage& = delete;
        S3ObjectStorage(S3ObjectStorage&&) = delete;
        auto operator=(S3ObjectStorage&&) -> S3ObjectStorage& = delete;

        [[nodiscard]]
        auto EnsureUploadTempDir(const std::string& upload_id)
            -> drogon::Task<Result<void>> override;

        [[nodiscard]]
        auto WriteChunk(
            const std::string& upload_id,
            uint32_t chunk_index,
            const std::string& md5_hash,
            std::string data
        ) -> drogon::Task<Result<UploadStagingChunk>> override;

        [[nodiscard]]
        auto AssembleChunks(
            const std::string& upload_id,
            uint32_t expected_chunk_count,
            const std::vector<UploadStagingChunk>& chunks
        )
            -> drogon::Task<Result<UploadStagingAssembly>> override;

        [[nodiscard]]
        auto DiscardAssembly(const std::string& upload_id, const UploadStagingAssembly& assembly)
            -> drogon::Task<Result<void>> override;

        [[nodiscard]]
        auto CleanupTemp(const std::string& upload_id) -> drogon::Task<Result<void>> override;

        [[nodiscard]]
        auto PromoteToFinal(const std::filesystem::path& temp_path, const std::string& hash)
            -> drogon::Task<Result<BlobPromoteResult>> override;

        [[nodiscard]]
        auto OpenForRead(const std::filesystem::path& storage_path)
            -> drogon::Task<Result<std::shared_ptr<std::ifstream>>> override;

        [[nodiscard]]
        auto OpenBlobRangeForRead(
            const BlobDescriptor& blob,
            uint64_t start,
            uint64_t length
        ) -> drogon::Task<Result<std::shared_ptr<StorageReadStream>>> override;

        [[nodiscard]]
        auto DeleteBlob(const std::filesystem::path& storage_path) -> drogon::Task<Result<void>> override;

        [[nodiscard]]
        auto Exists(const std::filesystem::path& storage_path) -> drogon::Task<Result<bool>> override;

        [[nodiscard]]
        auto GetFinalStoragePath(const std::string& hash) const -> std::filesystem::path override;

        [[nodiscard]]
        auto GetFileSize(const std::filesystem::path& storage_path) -> drogon::Task<Result<uint64_t>> override;

    private:
        [[nodiscard]]
        auto ToObjectKey(const std::filesystem::path& path) const -> std::string;

        std::shared_ptr<disk::utils::ConfigMgr> m_config_mgr;
        disk::utils::S3StorageConfig m_s3_config;
        std::shared_ptr<IS3Client> m_s3_client;
        LocalFileStorage m_local_staging;
        std::shared_ptr<trantor::ConcurrentTaskQueue> m_worker_queue;
    };

} // namespace disk::storage
