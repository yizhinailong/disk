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

#include "services/TokenService.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/Response.hpp"

namespace disk::filters {

    using disk::utils::ConfigMgr;

    JwtAuthFilter::JwtAuthFilter()
        : m_token_service(std::make_unique<disk::auth::TokenService>(ConfigMgr::GetInstance()->GetJwtSecret(), drogon::app().getRedisClient())) {
        LOG_DEBUG << "JwtAuthFilter 初始化完成";
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

        auto verify_result = m_token_service->VerifyAccessToken(token);
        if (!verify_result) {
            co_return disk::Response::Error(verify_result.error());
        }

        auto [user_id, username] = verify_result.value();

        request->attributes()->insert("user_id", user_id);
        request->attributes()->insert("username", username);

        LOG_DEBUG << "JWT 认证成功: user_id=" << user_id << ", username=" << username;

        co_return nullptr;
    }
} // namespace disk::filters
