/**
 * @file ShareAuthFilter.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 分享令牌认证过滤器实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "ShareAuthFilter.hpp"

#include "utils/Response.hpp"

namespace disk::filters {

    using disk::services::TokenService;

    auto ShareAuthFilter::doFilter(const drogon::HttpRequestPtr& request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        const auto& share_token_header = request->getHeader("X-Share-Token");

        if (share_token_header.empty()) {
            co_return disk::Response::Error(disk::error::Code::TokenMissing);
        }

        auto verify_result =
            co_await TokenService::GetInstance()->VerifyShareTokenWithRedis("", share_token_header);

        if (!verify_result) {
            const auto& error = verify_result.error();
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

        LOG_DEBUG << "Share token authentication successful: share_code=" << claims.share_code
                  << ", share_id=" << claims.share_id;
        co_return nullptr;
    }

} // namespace disk::filters
