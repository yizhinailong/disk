/**
 * @file RateLimitFilter.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 回收站路由通用用户频率限制过滤器实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "RateLimitFilter.hpp"

#include <utility>

#include "filters/FilterLogContext.hpp"
#include "filters/RateLimitHelper.hpp"
#include "services/RedisService.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/RedisKeyPrefix.hpp"
#include "utils/Response.hpp"

namespace disk::filters {
    namespace {

        auto MakeRedisCounter() -> ApiRateLimitCounter {
            const auto redis_service = disk::services::RedisService::GetInstance();
            disk::services::RedisService::Initialize(drogon::app().getRedisClient());
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

    RateLimitFilter::RateLimitFilter()
        : RateLimitFilter(MakeRedisCounter()) {
    }

    RateLimitFilter::RateLimitFilter(ApiRateLimitCounter counter)
        : m_counter(std::move(counter)) {
    }

    auto RateLimitFilter::doFilter(const drogon::HttpRequestPtr& request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        /// 可观测性：记录请求处理开始时间
        auto start = std::chrono::steady_clock::now();
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
        const auto window = GetFixedWindowStart(WINDOW_SECONDS);
        const auto key = RedisKeyPrefix::BuildApiRateLimitKey(user_id, window);

        /// 使用 Lua 脚本原子递增计数并设置过期时间（单次 Redis 交互）
        auto incr_result = co_await m_counter(key, WINDOW_SECONDS, log_context);
        if (!incr_result) {
            Logger::Error(log_context)
                << "Redis IncrWithExpire failed: " << incr_result.error().message;
            /// Redis 失败时不阻止请求
            co_return nullptr;
        }

        const int64_t current_count = incr_result.value();

        /// 检查是否超过限制
        if (current_count > DEFAULT_LIMIT) {
            Logger::Warn(log_context)
                << "API rate limit: user_id=" << user_id << ", count=" << current_count;

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info(log_context)
                << "[rate_limit_filter] duration_us=" << duration_us
                << " outcome=failure user_id=" << user_id;

            co_return BuildRateLimitExceededResponse(
                DEFAULT_LIMIT,
                GetFixedWindowReset(window, WINDOW_SECONDS),
                false
            );
        }

        Logger::Debug(log_context)
            << "API rate limit check passed: user_id=" << user_id
            << ", count=" << current_count << "/" << DEFAULT_LIMIT;

        auto end = std::chrono::steady_clock::now();
        auto duration_us =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        Logger::Info(log_context)
            << "[rate_limit_filter] duration_us=" << duration_us
            << " outcome=success user_id=" << user_id;

        co_return nullptr;
    }

} // namespace disk::filters
