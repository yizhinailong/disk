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
#include <utility>

#include "filters/FilterLogContext.hpp"
#include "filters/RateLimitHelper.hpp"
#include "services/RedisService.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/RedisKeyPrefix.hpp"
#include "utils/Response.hpp"

namespace disk::filters {
    namespace {

        auto MakeRedisCounter() -> AdminRateLimitCounter {
            const auto redis_service = disk::services::RedisService::GetInstance();
            return [redis_service](const std::string& key, int window_seconds)
                       -> drogon::Task<Result<int64_t>> {
                co_return co_await CheckFixedWindowLimit(redis_service, key, window_seconds);
            };
        }

    } // namespace

    using disk::redis::RedisKeyPrefix;

    AdminRateLimitFilter::AdminRateLimitFilter()
        : AdminRateLimitFilter(MakeRedisCounter()) {
    }

    AdminRateLimitFilter::AdminRateLimitFilter(AdminRateLimitCounter counter)
        : m_counter(std::move(counter)) {
    }

    auto AdminRateLimitFilter::doFilter(const drogon::HttpRequestPtr& request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        const auto log_context = GetFilterLogContext(request);

        const auto& path = request->path();
        if (!path.starts_with("/api/admin/")) {
            co_return nullptr;
        }

        auto attrs = request->attributes();
        if (!attrs || !attrs->find("user_id")) {
            co_return nullptr;
        }

        const auto user_id = attrs->get<uint64_t>("user_id");
        const auto config = disk::utils::ConfigMgr::GetInstance();
        const auto configured_window = config->GetAdminRateLimitWindowSeconds();
        const auto window_seconds = configured_window > 0 ? configured_window : WINDOW_SECONDS;
        const auto window = GetFixedWindowStart(window_seconds);
        const auto key =
            std::string("rate:admin:") + std::to_string(user_id) + ":" + std::to_string(window);
        const auto configured_limit = config->GetAdminRateLimitPerMinute();
        const auto limit = configured_limit > 0 ? configured_limit : DEFAULT_LIMIT;

        auto incr_result = co_await m_counter(key, window_seconds);
        if (!incr_result) {
            Logger::Error(log_context)
                << "Redis IncrWithExpire failed: " << incr_result.error().message;
            co_return nullptr;
        }

        const int64_t current_count = incr_result.value();

        if (current_count > limit) {
            const auto reset_time = GetFixedWindowReset(window, window_seconds);
            Logger::Warn(log_context)
                << "Admin rate limit: user_id=" << user_id
                << ", count=" << current_count;

            co_return BuildRateLimitExceededResponse(limit, reset_time);
        }

        Logger::Debug(log_context)
            << "Admin rate limit check passed: user_id=" << user_id
            << ", count=" << current_count << "/" << limit;

        co_return nullptr;
    }

} // namespace disk::filters
