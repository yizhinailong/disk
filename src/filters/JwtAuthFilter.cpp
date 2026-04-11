/**
 * @file JwtAuthFilter.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief JWT 认证过滤器实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "JwtAuthFilter.hpp"

#include <chrono>

#include "utils/Response.hpp"

namespace disk::filters {

    using disk::services::TokenService;

    auto JwtAuthFilter::doFilter(const drogon::HttpRequestPtr& request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        auto start = std::chrono::steady_clock::now();

        const auto& auth_header = request->getHeader("Authorization");

        if (auth_header.empty()) {
            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            LOG_INFO << "[jwt_auth_filter] duration_us=" << duration_us << " outcome=failure";
            co_return disk::Response::Error(disk::error::Code::TokenMissing);
        }

        if (!auth_header.starts_with("Bearer ")) {
            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            LOG_INFO << "[jwt_auth_filter] duration_us=" << duration_us << " outcome=failure";
            co_return disk::Response::Error(disk::error::Code::TokenMalformed);
        }

        const auto token = auth_header.substr(7);

        auto verify_result = TokenService::GetInstance()->VerifyAccessToken(token);
        if (!verify_result) {
            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            LOG_INFO << "[jwt_auth_filter] duration_us=" << duration_us << " outcome=failure";
            co_return disk::Response::Error(verify_result.error());
        }

        const auto& claims = verify_result.value();

        if (co_await TokenService::GetInstance()->IsAccessTokenRevoked(claims.jti)) {
            LOG_WARN << "Token revoked: user_id=" << claims.user_id << ", jti=" << claims.jti;

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            LOG_INFO << "[jwt_auth_filter] duration_us=" << duration_us
                     << " outcome=failure user_id=" << claims.user_id;
            co_return disk::Response::Error(disk::error::Code::TokenRevoked);
        }

        request->attributes()->insert("user_id", claims.user_id);
        request->attributes()->insert("username", claims.username);

        auto end = std::chrono::steady_clock::now();
        auto duration_us =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        LOG_INFO << "[jwt_auth_filter] duration_us=" << duration_us
                 << " outcome=success user_id=" << claims.user_id;

        co_return nullptr;
    }

} // namespace disk::filters
