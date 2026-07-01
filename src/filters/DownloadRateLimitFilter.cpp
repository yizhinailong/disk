/**
 * @file DownloadRateLimitFilter.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 下载频率限制过滤器实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "DownloadRateLimitFilter.hpp"
#include "filters/RateLimitHelper.hpp"

#include <algorithm>

#include "utils/ErrorCode.hpp"
#include "utils/RedisKeyPrefix.hpp"
#include "utils/Response.hpp"

namespace disk::filters {

    using disk::redis::RedisKeyPrefix;

    DownloadRateLimitFilter::DownloadRateLimitFilter()
        : m_redis_service(disk::services::RedisService::GetInstance()) {
    }

    auto DownloadRateLimitFilter::doFilter(const drogon::HttpRequestPtr& request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        /// 仅对下载路径生效
        const auto& path = request->path();
        if (!path.starts_with("/api/file/download/")) {
            co_return nullptr;
        }

        /// 从 request attributes 获取 user_id（由 JwtAuthFilter 设置）
        auto attrs = request->attributes();
        if (!attrs || !attrs->find("user_id")) {
            co_return nullptr;
        }

        const auto user_id = attrs->get<uint64_t>("user_id");
        const auto window = GetFixedWindowStart(WINDOW_SECONDS);
        const auto key =
            std::string("rate:download:") + std::to_string(user_id) + ":" + std::to_string(window);

        auto incr_result = co_await CheckFixedWindowLimit(m_redis_service, key, WINDOW_SECONDS);
        if (!incr_result) {
            Logger::Error() << "Redis IncrWithExpire failed: " << incr_result.error().message;
            co_return nullptr;
        }

        const int64_t current_count = incr_result.value();

        if (current_count > DEFAULT_LIMIT) {
            const auto reset_time = GetFixedWindowReset(window, WINDOW_SECONDS);
            Logger::Warn() << "Download rate limit: user_id=" << user_id
                     << ", count=" << current_count;

            co_return BuildRateLimitExceededResponse(DEFAULT_LIMIT, reset_time);
        }

        Logger::Debug() << "Download rate limit check passed: user_id=" << user_id
                  << ", count=" << current_count << "/" << DEFAULT_LIMIT;

        co_return nullptr;
    }

} ///< namespace disk::filters
