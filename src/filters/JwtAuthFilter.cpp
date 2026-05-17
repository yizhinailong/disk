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
#include <functional>
#include <type_traits>

#include <drogon/utils/coroutine.h>

#include "utils/Response.hpp"

namespace disk::filters {

    namespace {

        template <typename Func>
        auto RunOnAuthCpuPool(Func func)
            -> drogon::Task<std::remove_cvref_t<std::invoke_result_t<Func&>>> {
            using ReturnType = std::remove_cvref_t<std::invoke_result_t<Func&>>;

            auto* resume_loop = trantor::EventLoop::getEventLoopOfCurrentThread();
            auto result = co_await drogon::queueInLoopCoro<ReturnType>(
                disk::services::detail::GetAuthCpuWorkLoop(),
                std::function<ReturnType()>([func = std::move(func)]() mutable -> ReturnType {
                    return func();
                })
            );

            if (resume_loop != nullptr &&
                resume_loop != trantor::EventLoop::getEventLoopOfCurrentThread()) {
                co_await drogon::switchThreadCoro(resume_loop);
            }

            co_return result;
        }

    } // namespace

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
        auto token_service = TokenService::GetInstance();

        auto verify_result = co_await RunOnAuthCpuPool([token_service, token]() {
            return token_service->VerifyAccessToken(token);
        });
        if (!verify_result) {
            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            LOG_INFO << "[jwt_auth_filter] duration_us=" << duration_us << " outcome=failure";
            co_return disk::Response::Error(verify_result.error());
        }

        const auto& claims = verify_result.value();

        if (co_await token_service->IsAccessTokenRevoked(claims.jti)) {
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
        request->attributes()->insert("role", claims.role);
        request->attributes()->insert("status", claims.status);

        auto end = std::chrono::steady_clock::now();
        auto duration_us =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        LOG_INFO << "[jwt_auth_filter] duration_us=" << duration_us
                 << " outcome=success user_id=" << claims.user_id;

        co_return nullptr;
    }

} // namespace disk::filters
