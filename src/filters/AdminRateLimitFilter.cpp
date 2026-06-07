/**
 * @file AdminRateLimitFilter.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 管理员接口频率限制过滤器实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "AdminRateLimitFilter.hpp"

#include <algorithm>

#include "utils/ErrorCode.hpp"
#include "utils/RedisKeyPrefix.hpp"
#include "utils/Response.hpp"

namespace disk::filters {

    using disk::redis::RedisKeyPrefix;

    AdminRateLimitFilter::AdminRateLimitFilter()
        : m_redis_service(disk::services::RedisService::GetInstance()) {
    }

    auto AdminRateLimitFilter::doFilter(const drogon::HttpRequestPtr& request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        const auto& path = request->path();
        if (!path.starts_with("/api/admin/")) {
            co_return nullptr;
        }

        auto attrs = request->attributes();
        if (!attrs || !attrs->find("user_id")) {
            co_return nullptr;
        }

        const auto user_id = attrs->get<uint64_t>("user_id");
        const auto window = GetCurrentWindow();
        const auto key =
            std::string("rate:admin:") + std::to_string(user_id) + ":" + std::to_string(window);

        auto incr_result = co_await m_redis_service->IncrWithExpire(key, WINDOW_SECONDS);
        if (!incr_result) {
            Logger::Error() << "Redis IncrWithExpire failed: " << incr_result.error().message;
            co_return nullptr;
        }

        const int64_t current_count = incr_result.value();

        if (current_count > DEFAULT_LIMIT) {
            const auto reset_time = GetResetTime(window);
            const auto now = std::chrono::system_clock::now();
            const auto now_seconds =
                std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            const auto retry_after = std::max<int64_t>(1, reset_time - now_seconds);

            Logger::Warn() << "Admin rate limit: user_id=" << user_id
                     << ", count=" << current_count;

            auto response = disk::Response::Error(disk::error::Code::TooManyRequests);
            response->addHeader("X-RateLimit-Limit", std::to_string(DEFAULT_LIMIT));
            response->addHeader("X-RateLimit-Remaining", "0");
            response->addHeader("X-RateLimit-Reset", std::to_string(reset_time));
            response->addHeader("Retry-After", std::to_string(retry_after));

            co_return response;
        }

        Logger::Debug() << "Admin rate limit check passed: user_id=" << user_id
                  << ", count=" << current_count << "/" << DEFAULT_LIMIT;

        co_return nullptr;
    }

} // namespace disk::filters
