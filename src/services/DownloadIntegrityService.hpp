/**
 * @file DownloadIntegrityService.hpp
 * @brief 下载 Blob 完整性预检与对账记录服务
 */

#pragma once

#include <cstdint>
#include <memory>

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>

#include "storage/BlobDescriptor.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::reconciliation {
    class StorageReconciliationService;
}

namespace disk::storage {
    class IBlobStore;
}

namespace disk::download {

    class IDownloadIntegrityService {
    public:
        virtual ~IDownloadIntegrityService() = default;

        [[nodiscard]]
        virtual auto Preflight(
            const disk::storage::BlobDescriptor& blob,
            uint64_t expected_size
        ) -> drogon::Task<Result<void>> = 0;

        virtual auto RecordOpenFailure(
            const disk::storage::BlobDescriptor& blob,
            ErrorCode error_code,
            uint64_t range_start,
            uint64_t expected_bytes
        ) -> drogon::Task<void> = 0;

        virtual auto RecordStreamInterruption(
            const disk::storage::BlobDescriptor& blob,
            uint64_t range_start,
            uint64_t expected_bytes,
            uint64_t delivered_bytes
        ) noexcept -> void = 0;
    };

    class DownloadIntegrityService final : public IDownloadIntegrityService {
    public:
        DownloadIntegrityService(
            drogon::orm::DbClientPtr db_client,
            disk::storage::IBlobStore* blob_store
        );

        [[nodiscard]]
        auto Preflight(
            const disk::storage::BlobDescriptor& blob,
            uint64_t expected_size
        ) -> drogon::Task<Result<void>> override;

        auto RecordOpenFailure(
            const disk::storage::BlobDescriptor& blob,
            ErrorCode error_code,
            uint64_t range_start,
            uint64_t expected_bytes
        ) -> drogon::Task<void> override;

        auto RecordStreamInterruption(
            const disk::storage::BlobDescriptor& blob,
            uint64_t range_start,
            uint64_t expected_bytes,
            uint64_t delivered_bytes
        ) noexcept -> void override;

    private:
        disk::storage::IBlobStore* m_blob_store{};
        std::shared_ptr<disk::reconciliation::StorageReconciliationService>
            m_reconciliation_service;
    };

} // namespace disk::download
