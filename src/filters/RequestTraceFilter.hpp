/**
 * @file RequestTraceFilter.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 请求追踪过滤器，为每个请求生成唯一 ID
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

        /// 生成 UUID v4
        [[nodiscard]]
        static auto GenerateRequestId() -> std::string;
    };
} ///< namespace disk::filters
