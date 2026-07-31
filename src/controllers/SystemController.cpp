/**
 * @file SystemController.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 系统控制器
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "SystemController.hpp"

#include "controllers/ControllerHelpers.hpp"
#include "services/ObservedDbClient.hpp"
#include "utils/Response.hpp"

namespace disk::system {

    SystemController::SystemController()
        : m_system_service(
              std::make_unique<SystemService>(
                  disk::metrics::ObserveDbClient(drogon::app().getDbClient())
              )
          ) {
    }

    auto SystemController::GetInfo(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        auto log_context = disk::controllers::GetRequestLogContext(request, "system_info");

        Logger::Debug(log_context)
            << "Received system info request: " << request->getPeerAddr().toIpPort();

        /// 确认 JwtAuthFilter 已建立认证上下文
        if (!request->attributes()->find("user_id")) {
            Logger::Warn(log_context) << "System info request missing user_id attribute";
            co_return Response::Error(ErrorInfo(ErrorCode::TokenMissing));
        }

        /// 获取系统信息
        auto info_result = co_await m_system_service->GetInfo(log_context);
        if (!info_result) {
            Logger::Error(log_context)
                << "Failed to get system info: " << info_result.error().message;
            co_return Response::Error(info_result.error());
        }

        /// 构造响应
        co_return Response::Success(info_result->ToJson());
    }

} // namespace disk::system
