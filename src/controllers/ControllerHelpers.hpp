/**
 * @file ControllerHelpers.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 控制器通用辅助函数
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <drogon/drogon.h>

#include "utils/LogHelper.hpp"

namespace disk::controllers {

    /**
     * @brief 获取 JWT 认证过滤器写入的用户 ID
     *
     * @note 调用方应仅在已通过 JwtAuthFilter 的路由中使用。
     *       保持与原有 controller 直接读取 attributes 的行为一致。
     */
    [[nodiscard]]
    inline auto GetAuthenticatedUserId(const drogon::HttpRequestPtr& request) -> uint64_t {
        return request->attributes()->get<uint64_t>("user_id");
    }

    /**
     * @brief Build explicit request correlation for coroutine-safe logging.
     */
    [[nodiscard]]
    inline auto GetRequestLogContext(
        const drogon::HttpRequestPtr& request,
        std::string_view operation
    ) -> disk::utils::LogContext {
        disk::utils::LogContext context;
        const auto attributes = request->attributes();
        if (attributes->find("request_id")) {
            context.request_id = attributes->get<std::string>("request_id");
        }
        if (!operation.empty()) {
            context.operation = std::string(operation);
        }
        return context;
    }

} // namespace disk::controllers
