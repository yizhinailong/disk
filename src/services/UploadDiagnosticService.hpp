/**
 * @file UploadDiagnosticService.hpp
 * @brief Read-only administrator diagnostics for upload sessions
 */

#pragma once

#include <drogon/orm/DbClient.h>

#include "dtos/UploadDiagnosticDto.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/LogHelper.hpp"

namespace disk::storage {
    class UploadStagingStorage;
}

namespace disk::upload {

    class UploadDiagnosticService final {
    public:
        UploadDiagnosticService(
            drogon::orm::DbClientPtr db_client,
            disk::storage::UploadStagingStorage* staging_storage
        );

        [[nodiscard]]
        auto Diagnose(
            const disk::admin::UploadDiagnosticRequest& request,
            disk::utils::LogContext log_context = {}
        ) const
            -> drogon::Task<Result<disk::admin::UploadDiagnosticResponse>>;

    private:
        drogon::orm::DbClientPtr m_db_client;
        disk::storage::UploadStagingStorage* m_staging_storage;
    };

} // namespace disk::upload
