/**
 * @file StorageJobContract.hpp
 * @brief 周期存储任务的持久化 payload 与去重键合同
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>

#include "services/StorageJobRepository.hpp"
#include "services/StorageReconciliationService.hpp"
#include "storage/UploadStagingStorage.hpp"

namespace disk::jobs {

    [[nodiscard]]
    auto BuildStagingCleanupJob(const disk::storage::UploadStagingSession& session)
        -> NewStorageJob;

    struct ExpireUploadsPageRequest {
        std::string scan_id;
        uint64_t page{ 0 };
        size_t limit{ kDefaultExpireUploadsPageSize };
    };

    [[nodiscard]]
    auto ValidateExpireUploadsPageRequest(const ExpireUploadsPageRequest& request)
        -> std::expected<void, std::string>;

    [[nodiscard]]
    auto BuildExpireUploadsJob(const ExpireUploadsPageRequest& request)
        -> std::expected<NewStorageJob, std::string>;

    [[nodiscard]]
    auto ParseExpireUploadsJob(const StorageJob& job)
        -> std::expected<ExpireUploadsPageRequest, std::string>;

    struct ExpireTrashPageRequest {
        std::string scan_id;
        uint64_t after_id{ 0 };
        size_t limit{ kDefaultExpireTrashPageSize };
    };

    [[nodiscard]]
    auto ValidateExpireTrashPageRequest(const ExpireTrashPageRequest& request)
        -> std::expected<void, std::string>;

    [[nodiscard]]
    auto BuildExpireTrashJob(const ExpireTrashPageRequest& request)
        -> std::expected<NewStorageJob, std::string>;

    [[nodiscard]]
    auto ParseExpireTrashJob(const StorageJob& job)
        -> std::expected<ExpireTrashPageRequest, std::string>;

    [[nodiscard]]
    auto BuildReconciliationCursorDigest(
        const disk::reconciliation::ReconciliationPageRequest& request
    ) -> std::string;

    [[nodiscard]]
    auto BuildStorageReconcileJob(
        const disk::reconciliation::ReconciliationPageRequest& request
    ) -> std::expected<NewStorageJob, std::string>;

    [[nodiscard]]
    auto ParseStorageReconcileJob(const StorageJob& job)
        -> std::expected<disk::reconciliation::ReconciliationPageRequest, std::string>;

} // namespace disk::jobs
