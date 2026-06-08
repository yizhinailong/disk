/**
 * @file RequestTraceFilter.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 请求追踪过滤器实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "RequestTraceFilter.hpp"

#include <random>

#include <drogon/utils/coroutine.h>

namespace disk::filters {

    auto RequestTraceFilter::GenerateRequestId() -> std::string {
        /// UUID v4: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
        thread_local std::random_device rd;
        thread_local std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> dist(0, 15);
        std::uniform_int_distribution<uint32_t> dist_y(8, 11);

        constexpr const char* hex = "0123456789abcdef";
        char buf[36];
        for (int i = 0; i < 8; ++i)  buf[i] = hex[dist(gen)];
        buf[8] = '-';
        for (int i = 9; i < 13; ++i) buf[i] = hex[dist(gen)];
        buf[13] = '-';
        buf[14] = '4'; ///< version 4
        for (int i = 15; i < 18; ++i) buf[i] = hex[dist(gen)];
        buf[18] = '-';
        buf[19] = hex[dist_y(gen)]; ///< variant: 8/9/a/b
        for (int i = 20; i < 23; ++i) buf[i] = hex[dist(gen)];
        buf[23] = '-';
        for (int i = 24; i < 36; ++i) buf[i] = hex[dist(gen)];

        return {buf, 36};
    }

    auto RequestTraceFilter::doFilter(const drogon::HttpRequestPtr& request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        auto request_id = GenerateRequestId();
        request->attributes()->insert("request_id", std::move(request_id));

        co_return nullptr;
    }

} ///< namespace disk::filters
