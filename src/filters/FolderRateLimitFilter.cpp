/**
 * @file FolderRateLimitFilter.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件夹接口频率限制过滤器实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "FolderRateLimitFilter.hpp"

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

        auto MakeRedisCounter() -> FolderRateLimitCounter {
            const auto redis_service = disk::services::RedisService::GetInstance();
            return [redis_service](
                       const std::string& key,
                       int window_seconds,
                       disk::utils::LogContext log_context
                   )
                       -> drogon::Task<Result<int64_t>> {
                co_return co_await CheckFixedWindowLimit(redis_service, key, window_seconds, log_context);
            };
        }

    } // namespace

    using disk::redis::RedisKeyPrefix;

    FolderRateLimitFilter::FolderRateLimitFilter()
        : FolderRateLimitFilter(MakeRedisCounter()) {
    }

    FolderRateLimitFilter::FolderRateLimitFilter(FolderRateLimitCounter counter)
        : m_counter(std::move(counter)) {
    }

    auto FolderRateLimitFilter::doFilter(const drogon::HttpRequestPtr& request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        const auto log_context = GetFilterLogContext(request);

        const auto& path = request->path();
        if (!path.starts_with("/api/folder/")) {
            co_return nullptr;
        }

        auto attrs = request->attributes();
        if (!attrs || !attrs->find("user_id")) {
            co_return nullptr;
        }

        const auto user_id = attrs->get<uint64_t>("user_id");
        const auto config = disk::utils::ConfigMgr::GetInstance();
        const auto configured_window = config->GetFolderRateLimitWindowSeconds();
        const auto window_seconds = configured_window > 0 ? configured_window : WINDOW_SECONDS;
        const auto window = GetFixedWindowStart(window_seconds);
        const auto key =
            std::string("rate:folder:") + std::to_string(user_id) + ":" + std::to_string(window);
        const auto configured_limit = config->GetFolderRateLimitPerMinute();
        const auto limit = configured_limit > 0 ? configured_limit : DEFAULT_LIMIT;

        auto incr_result = co_await m_counter(key, window_seconds, log_context);
        if (!incr_result) {
            Logger::Error(log_context)
                << "Redis IncrWithExpire failed: " << incr_result.error().message;
            co_return nullptr;
        }

        const int64_t current_count = incr_result.value();

        if (current_count > limit) {
            const auto reset_time = GetFixedWindowReset(window, window_seconds);
            Logger::Warn(log_context)
                << "Folder rate limit: user_id=" << user_id
                << ", count=" << current_count;

            co_return BuildRateLimitExceededResponse(limit, reset_time);
        }

        Logger::Debug(log_context)
            << "Folder rate limit check passed: user_id=" << user_id
            << ", count=" << current_count << "/" << limit;

        co_return nullptr;
    }

} // namespace disk::filters
