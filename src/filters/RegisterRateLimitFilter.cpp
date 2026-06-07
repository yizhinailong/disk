/**
 * @file RegisterRateLimitFilter.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 注册频率限制过滤器实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "RegisterRateLimitFilter.hpp"

#include <algorithm>

#include "utils/ErrorCode.hpp"
#include "utils/RedisKeyPrefix.hpp"
#include "utils/Response.hpp"

namespace disk::filters {

    using disk::redis::RedisKeyPrefix;

    RegisterRateLimitFilter::RegisterRateLimitFilter()
        : m_redis_service(disk::services::RedisService::GetInstance()) {
    }

    auto RegisterRateLimitFilter::doFilter(const drogon::HttpRequestPtr& request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        // 仅对注册路径生效
        const auto& path = request->path();
        if (path != "/api/auth/register") {
            co_return nullptr;
        }

        // 从请求获取 IP 地址
        const auto ip = RedisKeyPrefix::ExtractIPOnly(request->peerAddr().toIp());
        const auto window = GetCurrentWindow();
        const auto key =
            std::string("rate:register:") + ip + ":" + std::to_string(window);

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

            Logger::Warn() << "Register rate limit: ip=" << ip
                     << ", count=" << current_count;

            auto response = disk::Response::Error(disk::error::Code::TooManyRequests);
            response->addHeader("X-RateLimit-Limit", std::to_string(DEFAULT_LIMIT));
            response->addHeader("X-RateLimit-Remaining", "0");
            response->addHeader("X-RateLimit-Reset", std::to_string(reset_time));
            response->addHeader("Retry-After", std::to_string(retry_after));

            co_return response;
        }

        Logger::Debug() << "Register rate limit check passed: ip=" << ip
                  << ", count=" << current_count << "/" << DEFAULT_LIMIT;

        co_return nullptr;
    }

} // namespace disk::filters
