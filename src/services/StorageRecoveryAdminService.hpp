/**
 * @file StorageRecoveryAdminService.hpp
 * @brief Audited administrator recovery commands for uploads and storage
 */

#pragma once

#include <cstdint>
#include <string>

#include <drogon/orm/DbClient.h>

#include "dtos/StorageRecoveryAdminDto.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::recovery {

    struct RecoveryAuditContext final {
        uint64_t operator_id{ 0 };
        std::string ip_address;
        std::string user_agent;
    };

    class StorageRecoveryAdminService final {
    public:
        explicit StorageRecoveryAdminService(drogon::orm::DbClientPtr db_client);

        [[nodiscard]]
        auto ReleaseUploadLease(
            const disk::admin::UploadLeaseReleaseRequest& request,
            const RecoveryAuditContext& audit
        ) const -> drogon::Task<Result<disk::admin::UploadLeaseReleaseResponse>>;

        [[nodiscard]]
        auto RebuildUploadCleanup(
            const disk::admin::UploadCleanupRebuildRequest& request,
            const RecoveryAuditContext& audit
        ) const -> drogon::Task<Result<disk::admin::UploadCleanupRebuildResponse>>;

        [[nodiscard]]
        auto EnqueueReconciliation(
            const disk::admin::StorageReconciliationEnqueueRequest& request,
            const RecoveryAuditContext& audit
        ) const -> drogon::Task<Result<disk::admin::StorageReconciliationEnqueueResponse>>;

    private:
        drogon::orm::DbClientPtr m_db_client;
    };

} // namespace disk::recovery
