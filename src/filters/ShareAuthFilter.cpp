/**
 * @file ShareAuthFilter.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 分享令牌认证过滤器实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "ShareAuthFilter.hpp"

#include <chrono>

#include "filters/FilterLogContext.hpp"
#include "utils/Response.hpp"

namespace disk::filters {

    using disk::services::TokenService;

    namespace {

        [[nodiscard]] auto RequiresDownloadScope(const std::string& path) -> bool {
            return path.rfind("/api/share/download/", 0) == 0 ||
                   path.rfind("/api/share/save/", 0) == 0;
        }

    } // namespace

    auto ShareAuthFilter::doFilter(const drogon::HttpRequestPtr& request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        const auto log_context = GetFilterLogContext(request);
        auto start = std::chrono::steady_clock::now();

        const auto& share_token_header = request->getHeader("X-Share-Token");

        if (share_token_header.empty()) {
            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info(log_context) << "[share_auth_filter] duration_us=" << duration_us << " outcome=failure";
            co_return disk::Response::Error(disk::error::Code::TokenMissing);
        }

        auto verify_result =
            co_await TokenService::GetInstance()->VerifyShareTokenWithRedis(
                "",
                share_token_header,
                log_context
            );

        if (!verify_result) {
            const auto& error = verify_result.error();

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info(log_context) << "[share_auth_filter] duration_us=" << duration_us << " outcome=failure";

            switch (error.code) {
                case disk::error::Code::TokenExpired:
                    co_return disk::Response::Error(disk::error::Code::TokenExpired);
                case disk::error::Code::TokenRevoked:
                    co_return disk::Response::Error(disk::error::Code::TokenRevoked);
                case disk::error::Code::RedisOperationFailed:
                    co_return disk::Response::Error(disk::error::Code::RedisOperationFailed);
                case disk::error::Code::TokenMalformed:
                default                               : co_return disk::Response::Error(disk::error::Code::TokenMalformed);
            }
        }

        const auto& claims = verify_result.value();
        if (RequiresDownloadScope(request->path()) && claims.scope.permission != "download") {
            Logger::Warn(log_context) << "Share token scope does not permit operation: share_code="
                                      << claims.share_code << ", permission=" << claims.scope.permission
                                      << ", path=" << request->path();
            co_return disk::Response::Error(disk::error::Code::ShareAccessDenied);
        }

        request->attributes()->insert("share_code", claims.share_code);
        request->attributes()->insert("share_id", claims.share_id);
        request->attributes()->insert(SHARE_TOKEN_JTI_ATTRIBUTE, claims.jti);

        auto end = std::chrono::steady_clock::now();
        auto duration_us =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        Logger::Info(log_context) << "[share_auth_filter] duration_us=" << duration_us
                                  << " outcome=success share_code=" << claims.share_code;

        co_return nullptr;
    }

} // namespace disk::filters
