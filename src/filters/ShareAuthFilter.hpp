/**
 * @file ShareAuthFilter.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 分享令牌认证过滤器
 * @version 0.1
 * @date 2026-02-15
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <drogon/HttpFilter.h>
#include <drogon/HttpRequest.h>

#include "services/TokenService.hpp"

namespace disk::filters {
    class ShareAuthFilter : public drogon::HttpCoroFilter<ShareAuthFilter> {
    public:
        ShareAuthFilter();

        [[nodiscard]]
        auto doFilter(const drogon::HttpRequestPtr& request)
            -> drogon::Task<drogon::HttpResponsePtr> override;

    private:
    };
} // namespace disk::filters
