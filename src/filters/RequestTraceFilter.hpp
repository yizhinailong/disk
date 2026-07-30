/**
 * @file RequestTraceFilter.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 请求追踪过滤器，采用安全的上游 ID 或生成唯一 ID
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <string>

namespace disk::filters {
    class RequestTraceFilter : public drogon::HttpCoroFilter<RequestTraceFilter> {
    public:
        [[nodiscard]]
        auto doFilter(const drogon::HttpRequestPtr& request)
            -> drogon::Task<drogon::HttpResponsePtr> override;

        /// 优先采用合法请求头，否则生成 UUID
        [[nodiscard]]
        static auto ResolveRequestId(const drogon::HttpRequestPtr& request) -> std::string;
    };
} // namespace disk::filters
