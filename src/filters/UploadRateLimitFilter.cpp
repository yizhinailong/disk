/**
 * @file UploadRateLimitFilter.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 上传频率限制过滤器实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "UploadRateLimitFilter.hpp"

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

        auto MakeRedisCounter() -> UploadRateLimitCounter {
            const auto redis_service = disk::services::RedisService::GetInstance();
            disk::services::RedisService::Initialize(drogon::app().getRedisClient());
            return [redis_service](const std::string& key, int window_seconds)
                       -> drogon::Task<Result<int64_t>> {
                co_return co_await CheckFixedWindowLimit(redis_service, key, window_seconds);
            };
        }

    } // namespace

    using disk::redis::RedisKeyPrefix;

    UploadRateLimitFilter::UploadRateLimitFilter()
        : UploadRateLimitFilter(MakeRedisCounter()) {
    }

    UploadRateLimitFilter::UploadRateLimitFilter(UploadRateLimitCounter counter)
        : m_counter(std::move(counter)) {
    }

    auto UploadRateLimitFilter::doFilter(const drogon::HttpRequestPtr& request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        const auto log_context = GetFilterLogContext(request);

        /// 从 request attributes 获取 user_id（由 JwtAuthFilter 设置）
        auto attrs = request->attributes();
        if (!attrs) {
            Logger::Warn(log_context) << "Cannot get request attributes";
            co_return nullptr;
        }

        if (!attrs->find("user_id")) {
            /// 没有 user_id，跳过频率限制（可能是 exempt 的路径）
            co_return nullptr;
        }

        const auto user_id = attrs->get<uint64_t>("user_id");
        const auto config = disk::utils::ConfigMgr::GetInstance();
        const auto configured_window = config->GetUploadRateLimitWindowSeconds();
        const auto window_seconds = configured_window > 0 ? configured_window : WINDOW_SECONDS;
        const auto window = GetFixedWindowStart(window_seconds);
        const auto key = RedisKeyPrefix::BuildUploadRateLimitKey(user_id, window);
        const auto configured_limit = config->GetUploadRateLimitPerMinute();
        const auto limit = configured_limit > 0 ? configured_limit : DEFAULT_LIMIT;

        /// 使用 Lua 脚本原子递增计数并设置过期时间（单次 Redis 交互）
        auto incr_result = co_await m_counter(key, window_seconds);
        if (!incr_result) {
            Logger::Error(log_context)
                << "Redis IncrWithExpire failed: " << incr_result.error().message;
            /// Redis 失败时不阻止请求
            co_return nullptr;
        }

        const int64_t current_count = incr_result.value();

        /// 检查是否超过限制
        if (current_count > limit) {
            const auto reset_time = GetFixedWindowReset(window, window_seconds);
            Logger::Warn(log_context)
                << "Upload rate limit: user_id=" << user_id
                << ", path=" << request->path()
                << ", count=" << current_count;

            co_return BuildRateLimitExceededResponse(limit, reset_time);
        }

        Logger::Debug(log_context)
            << "Upload rate limit check passed: user_id=" << user_id
            << ", count=" << current_count << "/" << limit;

        co_return nullptr;
    }

} // namespace disk::filters
