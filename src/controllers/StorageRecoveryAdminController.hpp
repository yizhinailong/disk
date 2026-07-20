/**
 * @file StorageRecoveryAdminController.hpp
 * @brief Administrator HTTP boundary for audited storage recovery commands
 */

#pragma once

namespace disk::controllers {

    class StorageRecoveryAdminController final
        : public drogon::HttpController<StorageRecoveryAdminController> {
    public:
        METHOD_LIST_BEGIN
        ADD_METHOD_TO(
            StorageRecoveryAdminController::ReleaseUploadLease,
            "/api/admin/uploads/{upload_id}/lease/release",
            drogon::Post,
            "disk::filters::AdminAuthFilter",
            "disk::filters::AdminRateLimitFilter"
        );
        ADD_METHOD_TO(
            StorageRecoveryAdminController::RebuildUploadCleanup,
            "/api/admin/uploads/{upload_id}/cleanup/rebuild",
            drogon::Post,
            "disk::filters::AdminAuthFilter",
            "disk::filters::AdminRateLimitFilter"
        );
        ADD_METHOD_TO(
            StorageRecoveryAdminController::EnqueueReconciliation,
            "/api/admin/storage-reconciliation/{scan_id}/enqueue",
            drogon::Post,
            "disk::filters::AdminAuthFilter",
            "disk::filters::AdminRateLimitFilter"
        );
        METHOD_LIST_END

        [[nodiscard]]
        auto ReleaseUploadLease(drogon::HttpRequestPtr request, std::string upload_id)
            -> drogon::Task<drogon::HttpResponsePtr>;

        [[nodiscard]]
        auto RebuildUploadCleanup(drogon::HttpRequestPtr request, std::string upload_id)
            -> drogon::Task<drogon::HttpResponsePtr>;

        [[nodiscard]]
        auto EnqueueReconciliation(drogon::HttpRequestPtr request, std::string scan_id)
            -> drogon::Task<drogon::HttpResponsePtr>;
    };

} // namespace disk::controllers
