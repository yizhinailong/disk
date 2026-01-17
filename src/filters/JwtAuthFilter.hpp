/**
 * @file JwtAuthFilter.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief   JWT 认证过滤器
 * @version 0.1
 * @date 2026-01-17
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include "services/TokenService.hpp"

namespace disk::filters {
    class JwtAuthFilter : public drogon::HttpCoroFilter<JwtAuthFilter> {
    public:
        JwtAuthFilter();

        auto doFilter(const drogon::HttpRequestPtr& request)
            -> drogon::Task<drogon::HttpResponsePtr> override;

    private:
        std::unique_ptr<disk::auth::TokenService> m_token_service;
    };
} // namespace disk::filters
