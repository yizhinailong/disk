/**
 * @file SystemController.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 系统控制器
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include "services/SystemService.hpp"

namespace disk::system {

    class SystemController : public drogon::HttpController<SystemController> {
    public:
        SystemController();

        METHOD_LIST_BEGIN
        ADD_METHOD_TO(
            SystemController::GetInfo,
            "/api/system/info",
            drogon::Get,
            "disk::filters::JwtAuthFilter",
        );
        METHOD_LIST_END

        [[nodiscard]]
        auto GetInfo(drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr>;

    private:
        std::unique_ptr<SystemService> m_system_service{};
    };

} ///< namespace disk::system
