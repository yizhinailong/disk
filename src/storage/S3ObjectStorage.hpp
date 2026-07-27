#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "storage/IBlobStore.hpp"
#include "storage/LocalFileStorage.hpp"
#include "storage/MultipartUploadRecovery.hpp"
#include "storage/S3Client.hpp"
#include "storage/UploadStagingStorage.hpp"
#include "utils/ConfigMgr.hpp"

namespace trantor {
    class ConcurrentTaskQueue;
}

namespace disk::storage {

    class S3ObjectStorage final : public IBlobStore,
                                  public UploadStagingStorage,
                                  public IMultipartUploadCleaner {
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
        auto EnsureUploadSession(
            const UploadStagingSession& session,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<void>> override;

        [[nodiscard]]
        auto WriteChunk(
            const UploadStagingSession& session,
            uint32_t chunk_index,
            const std::string& md5_hash,
            std::string data,
            disk::utils::LogContext log_context = {}
        ) -> drogon::Task<Result<UploadStagingChunk>> override;

        [[nodiscard]]
        auto HeadChunkObject(
            const UploadStagingSession& session,
            const UploadStagingChunk& chunk,
            disk::utils::LogContext log_context = {}
        ) -> drogon::Task<Result<UploadStagingObjectHead>> override;

        [[nodiscard]]
        auto AssembleChunks(
            const UploadStagingSession& session,
            uint64_t state_version,
            uint32_t expected_chunk_count,
            const std::vector<UploadStagingChunk>& chunks,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<UploadStagingAssembly>> override;

        [[nodiscard]]
        auto DiscardAssembly(
            const UploadStagingSession& session,
            const UploadStagingAssembly& assembly,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<void>> override;

        [[nodiscard]]
        auto CleanupSession(
            const UploadStagingSession& session,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<void>> override;

        [[nodiscard]]
        auto ListStagingObjects(
            const std::string& continuation_token,
            size_t limit,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<StorageInventoryPage>> override;

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
        auto GetFileSize(
            const std::filesystem::path& storage_path,
            disk::utils::LogContext log_context = {}
        ) -> drogon::Task<Result<uint64_t>> override;

        [[nodiscard]]
        auto ListFinalObjects(
            const std::string& continuation_token,
            size_t limit,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<StorageInventoryPage>> override;

        [[nodiscard]]
        auto AbortMultipartUpload(
            const MultipartUploadDescriptor& descriptor,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<void>> override;

        auto SetMultipartUploadJournal(std::shared_ptr<IMultipartUploadJournal> journal) -> void;

    private:
        std::shared_ptr<disk::utils::ConfigMgr> m_config_mgr;
        disk::utils::S3StorageConfig m_s3_config;
        std::shared_ptr<IS3Client> m_s3_client;
        std::shared_ptr<IMultipartUploadJournal> m_multipart_upload_journal;
        LocalFileStorage m_local_staging;
        std::shared_ptr<trantor::ConcurrentTaskQueue> m_worker_queue;
    };

} // namespace disk::storage
