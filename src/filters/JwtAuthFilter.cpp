/**
 * @file JwtAuthFilter.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief JWT 认证过滤器实现
 * @version 0.1
 * @date 2026-01-17
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "JwtAuthFilter.hpp"

#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/traits.h>

#include "utils/ConfigMgr.hpp"
#include "utils/Response.hpp"

namespace disk::filters {

    using disk::utils::ConfigMgr;
    using disk::services::TokenService;
    JwtAuthFilter::JwtAuthFilter() {
        // Initialize TokenService singleton
        disk::services::TokenService::Initialize(ConfigMgr::GetInstance()->GetJwtSecret());

    }

    auto JwtAuthFilter::doFilter(const drogon::HttpRequestPtr& request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        const auto& auth_header = request->getHeader("Authorization");

        if (auth_header.empty()) {
            co_return disk::Response::Error(disk::error::Code::TokenMissing);
        }

        if (!auth_header.starts_with("Bearer ")) {
            co_return disk::Response::Error(disk::error::Code::TokenMalformed);
        }

        const auto token = auth_header.substr(7);

        auto verify_result = TokenService::GetInstance()->VerifyAccessToken(token);
        if (!verify_result) {
            co_return disk::Response::Error(verify_result.error());
        }

        auto [user_id, username] = verify_result.value();

        // NEW: 检查令牌是否被撤销（access 和 refresh token 现在都有 JTI）
        using traits = jwt::traits::open_source_parsers_jsoncpp;
        auto decoded = jwt::decode<traits>(token);

        if (decoded.has_payload_claim("jti")) {
            const auto jti = decoded.get_payload_claim("jti").as_string();

            if (co_await TokenService::GetInstance()->IsAccessTokenRevoked(jti)) {
                LOG_WARN << "Token revoked: user_id=" << user_id << ", jti=" << jti;
                co_return disk::Response::Error(disk::error::Code::TokenRevoked);
            }
        }

        request->attributes()->insert("user_id", user_id);
        request->attributes()->insert("username", username);

        LOG_DEBUG << "JWT authentication successful: user_id=" << user_id
                  << ", username=" << username;
        co_return nullptr;
    }

} // namespace disk::filters
