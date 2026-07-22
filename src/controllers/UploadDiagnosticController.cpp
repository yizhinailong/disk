/**
 * @file UploadDiagnosticController.cpp
 * @brief Administrator HTTP boundary for read-only upload diagnostics
 */

#include "controllers/UploadDiagnosticController.hpp"

#include "controllers/ControllerHelpers.hpp"
#include "dtos/UploadDiagnosticDto.hpp"
#include "services/ObservedDbClient.hpp"
#include "services/UploadDiagnosticService.hpp"
#include "storage/StorageMgr.hpp"
#include "utils/Response.hpp"

namespace disk::controllers {

    auto UploadDiagnosticController::Get(
        drogon::HttpRequestPtr request,
        std::string upload_id
    ) -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = GetRequestLogContext(request, "admin");
        auto parsed = disk::admin::UploadDiagnosticRequest::FromRequest(request, upload_id);
        if (!parsed) {
            co_return Response::Error(parsed.error());
        }
        log_context.upload_id = parsed->upload_id;
        Logger::Info(log_context) << "Received upload diagnostic request";

        disk::upload::UploadDiagnosticService service(
            disk::metrics::ObserveDbClient(drogon::app().getDbClient()),
            disk::storage::StorageMgr::GetUploadStagingStorage()
        );
        auto result = co_await service.Diagnose(parsed.value(), log_context);
        co_return result ? Response::Success(result->ToJson()) : Response::Error(result.error());
    }

} // namespace disk::controllers
