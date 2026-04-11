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

#include "utils/Response.hpp"

namespace disk::filters {

    using disk::services::TokenService;

    auto ShareAuthFilter::doFilter(const drogon::HttpRequestPtr& request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        auto start = std::chrono::steady_clock::now();

        const auto& share_token_header = request->getHeader("X-Share-Token");

        if (share_token_header.empty()) {
            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            LOG_INFO << "[share_auth_filter] duration_us=" << duration_us << " outcome=failure";
            co_return disk::Response::Error(disk::error::Code::TokenMissing);
        }

        auto verify_result =
            co_await TokenService::GetInstance()->VerifyShareTokenWithRedis("", share_token_header);

        if (!verify_result) {
            const auto& error = verify_result.error();

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            LOG_INFO << "[share_auth_filter] duration_us=" << duration_us << " outcome=failure";

            switch (error.code) {
                case disk::error::Code::TokenExpired:
                    co_return disk::Response::Error(disk::error::Code::TokenExpired);
                case disk::error::Code::TokenRevoked:
                    co_return disk::Response::Error(disk::error::Code::TokenRevoked);
                case disk::error::Code::TokenMalformed:
                default                               : co_return disk::Response::Error(disk::error::Code::TokenMalformed);
            }
        }

        const auto& claims = verify_result.value();
        request->attributes()->insert("share_code", claims.share_code);
        request->attributes()->insert("share_id", claims.share_id);

        auto end = std::chrono::steady_clock::now();
        auto duration_us =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        LOG_INFO << "[share_auth_filter] duration_us=" << duration_us
                 << " outcome=success share_code=" << claims.share_code;

        co_return nullptr;
    }

} // namespace disk::filters
