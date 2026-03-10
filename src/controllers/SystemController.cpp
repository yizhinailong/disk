/**
 * @file SystemController.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 系统控制器
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "SystemController.hpp"

#include "utils/Response.hpp"

namespace disk::system {

    SystemController::SystemController()
        : m_system_service(
              std::make_unique<SystemService>(
                  drogon::app().getDbClient(),
                  drogon::app().getRedisClient()
              )
          ) {
    }

    auto SystemController::GetInfo(drogon::HttpRequestPtr request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        LOG_DEBUG << "Received system info request: " << request->getPeerAddr().toIpPort();

        // 提取 user_id（由 JwtAuthFilter 设置）
        if (!request->attributes()->find("user_id")) {
            LOG_WARN << "System info request missing user_id attribute";
            co_return Response::Error(ErrorInfo(ErrorCode::TokenMissing));
        }
        const auto user_id = request->attributes()->get<uint64_t>("user_id");

        // 获取系统信息
        auto info_result = co_await m_system_service->GetInfo(user_id);
        if (!info_result) {
            LOG_ERROR << "Failed to get system info: " << info_result.error().message;
            co_return Response::Error(info_result.error());
        }

        // 构造响应
        Json::Value data;
        data["version"] = info_result->version;
        data["drogon_version"] = info_result->drogon_version;
        data["build_time"] = info_result->build_time;
        data["uptime"] = info_result->uptime;

        Json::Value connections;
        connections["current"] = info_result->connections.current;
        connections["peak"] = info_result->connections.peak;
        data["connections"] = connections;

        Json::Value storage;
        storage["total_users"] = info_result->storage.total_users;
        storage["total_files"] = info_result->storage.total_files;
        storage["total_folders"] = info_result->storage.total_folders;
        storage["total_size"] = info_result->storage.total_size;
        data["storage"] = storage;

        co_return Response::Success(data);
    }

} // namespace disk::system
