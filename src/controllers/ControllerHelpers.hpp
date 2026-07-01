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

#include <drogon/drogon.h>

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

} ///< namespace disk::controllers
