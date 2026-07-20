/**
 * @file UploadDiagnosticController.hpp
 * @brief Administrator HTTP boundary for read-only upload diagnostics
 */

#pragma once

namespace disk::controllers {

    class UploadDiagnosticController final
        : public drogon::HttpController<UploadDiagnosticController> {
    public:
        METHOD_LIST_BEGIN
        ADD_METHOD_TO(
            UploadDiagnosticController::Get,
            "/api/admin/uploads/{upload_id}/diagnostics",
            drogon::Get,
            "disk::filters::AdminAuthFilter",
            "disk::filters::AdminRateLimitFilter"
        );
        METHOD_LIST_END

        [[nodiscard]]
        auto Get(drogon::HttpRequestPtr request, std::string upload_id)
            -> drogon::Task<drogon::HttpResponsePtr>;
    };

} // namespace disk::controllers
