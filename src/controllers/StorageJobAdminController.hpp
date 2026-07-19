/**
 * @file StorageJobAdminController.hpp
 * @brief Administrator HTTP boundary for persistent storage jobs
 */

#pragma once

namespace disk::controllers {

    class StorageJobAdminController final
        : public drogon::HttpController<StorageJobAdminController> {
    public:
        METHOD_LIST_BEGIN
        ADD_METHOD_TO(
            StorageJobAdminController::List,
            "/api/admin/storage-jobs",
            drogon::Get,
            "disk::filters::AdminAuthFilter",
            "disk::filters::AdminRateLimitFilter"
        );
        ADD_METHOD_TO(
            StorageJobAdminController::Get,
            "/api/admin/storage-jobs/{id}",
            drogon::Get,
            "disk::filters::AdminAuthFilter",
            "disk::filters::AdminRateLimitFilter"
        );
        ADD_METHOD_TO(
            StorageJobAdminController::Replay,
            "/api/admin/storage-jobs/{id}/replay",
            drogon::Post,
            "disk::filters::AdminAuthFilter",
            "disk::filters::AdminRateLimitFilter"
        );
        METHOD_LIST_END

        [[nodiscard]]
        auto List(drogon::HttpRequestPtr request)
            -> drogon::Task<drogon::HttpResponsePtr>;

        [[nodiscard]]
        auto Get(drogon::HttpRequestPtr request, std::string id)
            -> drogon::Task<drogon::HttpResponsePtr>;

        [[nodiscard]]
        auto Replay(drogon::HttpRequestPtr request, std::string id)
            -> drogon::Task<drogon::HttpResponsePtr>;
    };

} // namespace disk::controllers
