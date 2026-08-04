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
#include <string>

#include <drogon/utils/coroutine.h>

#include "filters/FilterLogContext.hpp"
#include "utils/AuthCpuPool.hpp"
#include "utils/Response.hpp"

namespace disk::filters {

    namespace {
        [[nodiscard]]
        auto IsPublicPath(const std::string& path) -> bool {
            return path == "/api/auth/register" || path == "/api/auth/login" ||
                   path == "/api/auth/refresh" || path == "/api/health" ||
                   path == "/api/health/live" || path == "/api/health/ready" ||
                   path == "/metrics" ||
                   path.rfind("/api/share/access/", 0) == 0 ||
                   path.rfind("/api/share/browse/", 0) == 0 ||
                   path.rfind("/api/share/download/", 0) == 0;
        }
    } // namespace

    using disk::services::TokenService;
    using disk::utils::RunOnAuthCpuPool;

    auto JwtAuthFilter::doFilter(const drogon::HttpRequestPtr& request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        const auto log_context = GetFilterLogContext(request);
        auto start = std::chrono::steady_clock::now();

        if (IsPublicPath(request->path())) {
            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info(log_context) << "[jwt_auth_filter] duration_us=" << duration_us
                                      << " outcome=exempt path=" << request->path();
            co_return nullptr;
        }

        const auto& auth_header = request->getHeader("Authorization");

        if (auth_header.empty()) {
            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info(log_context) << "[jwt_auth_filter] duration_us=" << duration_us << " outcome=failure";
            co_return disk::Response::Error(disk::error::Code::TokenMissing);
        }

        if (!auth_header.starts_with("Bearer ")) {
            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info(log_context) << "[jwt_auth_filter] duration_us=" << duration_us << " outcome=failure";
            co_return disk::Response::Error(disk::error::Code::TokenMalformed);
        }

        const auto token = auth_header.substr(7);
        auto token_service = TokenService::GetInstance();

        auto verify_result = co_await RunOnAuthCpuPool([token_service, token, log_context]() {
            return token_service->VerifyAccessToken(token, log_context);
        });
        if (!verify_result) {
            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info(log_context) << "[jwt_auth_filter] duration_us=" << duration_us << " outcome=failure";
            co_return disk::Response::Error(verify_result.error());
        }

        const auto& claims = verify_result.value();

        auto revocation_result =
            co_await token_service->IsAccessTokenRevoked(claims.jti, log_context);
        if (!revocation_result) {
            Logger::Error(log_context) << "Access token revocation check failed";
            co_return disk::Response::Error(revocation_result.error().code);
        }

        if (revocation_result.value()) {
            Logger::Warn(log_context) << "Access token has been revoked";

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info(log_context) << "[jwt_auth_filter] duration_us=" << duration_us
                                      << " outcome=failure";
            co_return disk::Response::Error(disk::error::Code::TokenRevoked);
        }

        request->attributes()->insert("user_id", claims.user_id);
        request->attributes()->insert("username", claims.username);
        request->attributes()->insert("role", claims.role);
        request->attributes()->insert("status", claims.status);

        auto end = std::chrono::steady_clock::now();
        auto duration_us =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        Logger::Info(log_context) << "[jwt_auth_filter] duration_us=" << duration_us
                                  << " outcome=success";

        co_return nullptr;
    }

} // namespace disk::filters
