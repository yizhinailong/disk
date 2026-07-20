/**
 * @file StorageJobAdminService.hpp
 * @brief Audited administrator operations for persistent storage jobs
 */

#pragma once

#include <cstdint>
#include <string>

#include <drogon/orm/DbClient.h>

#include "dtos/StorageJobAdminDto.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::jobs {

    struct StorageJobAuditContext final {
        uint64_t operator_id{ 0 };
        std::string ip_address;
        std::string user_agent;
    };

    class StorageJobAdminService final {
    public:
        explicit StorageJobAdminService(drogon::orm::DbClientPtr db_client);

        [[nodiscard]]
        auto List(const disk::admin::StorageJobListRequest& request) const
            -> drogon::Task<Result<disk::admin::StorageJobListResponse>>;

        [[nodiscard]]
        auto Get(uint64_t job_id) const
            -> drogon::Task<Result<disk::admin::StorageJobItem>>;

        [[nodiscard]]
        auto ListRelatedToUpload(
            const std::string& upload_id,
            const std::string& staging_prefix,
            int page,
            int page_size
        ) const -> drogon::Task<Result<disk::admin::StorageJobListResponse>>;

        [[nodiscard]]
        auto Replay(
            uint64_t job_id,
            const disk::admin::StorageJobReplayRequest& request,
            const StorageJobAuditContext& audit
        ) const -> drogon::Task<Result<disk::admin::StorageJobReplayResponse>>;

    private:
        drogon::orm::DbClientPtr m_db_client;
    };

} // namespace disk::jobs
