/**
 * @file RateLimitFilter.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief API 频率限制过滤器实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "RateLimitFilter.hpp"

#include "utils/ErrorCode.hpp"
#include "utils/RedisKeyPrefix.hpp"
#include "utils/Response.hpp"

namespace disk::filters {

    using disk::redis::RedisKeyPrefix;

    RateLimitFilter::RateLimitFilter()
        : m_redis_service(disk::services::RedisService::GetInstance()) {
        // 初始化 RedisService 单例（如果尚未初始化）
        disk::services::RedisService::Initialize(drogon::app().getRedisClient());
    }

    auto RateLimitFilter::doFilter(const drogon::HttpRequestPtr& request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        // 可观测性：记录请求处理开始时间
        auto start = std::chrono::steady_clock::now();

        // 从 request attributes 获取 user_id（由 JwtAuthFilter 设置）
        auto attrs = request->attributes();
        if (!attrs) {
            LOG_WARN << "Cannot get request attributes";
            co_return nullptr;
        }

        if (!attrs->find("user_id")) {
            // 没有 user_id，跳过频率限制（可能是 exempt 的路径）
            co_return nullptr;
        }

        const auto user_id = attrs->get<uint64_t>("user_id");
        const auto window = GetCurrentWindow();
        const auto key = RedisKeyPrefix::BuildApiRateLimitKey(user_id, window);

        // 使用 Lua 脚本原子递增计数并设置过期时间（单次 Redis 交互）
        auto incr_result = co_await m_redis_service->IncrWithExpire(key, WINDOW_SECONDS);
        if (!incr_result) {
            LOG_ERROR << "Redis IncrWithExpire failed: " << incr_result.error().message;
            // Redis 失败时不阻止请求
            co_return nullptr;
        }

        const int64_t current_count = incr_result.value();

        // 检查是否超过限制
        if (current_count > DEFAULT_LIMIT) {
            LOG_WARN << "API rate limit: user_id=" << user_id << ", count=" << current_count;

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            LOG_INFO << "[rate_limit_filter] duration_us=" << duration_us
                     << " outcome=failure user_id=" << user_id;

            auto response = disk::Response::Error(disk::error::Code::TooManyRequests);
            response->addHeader("X-RateLimit-Limit", std::to_string(DEFAULT_LIMIT));
            response->addHeader("X-RateLimit-Remaining", "0");
            response->addHeader("X-RateLimit-Reset", std::to_string(GetResetTime(window)));

            co_return response;
        }

        LOG_DEBUG << "API rate limit check passed: user_id=" << user_id
                  << ", count=" << current_count << "/" << DEFAULT_LIMIT;

        auto end = std::chrono::steady_clock::now();
        auto duration_us =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        LOG_INFO << "[rate_limit_filter] duration_us=" << duration_us
                 << " outcome=success user_id=" << user_id;

        co_return nullptr;
    }

} // namespace disk::filters
