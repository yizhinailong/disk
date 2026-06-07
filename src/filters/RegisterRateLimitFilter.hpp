/**
 * @file RegisterRateLimitFilter.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 注册频率限制过滤器
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <chrono>

#include <drogon/HttpFilter.h>

#include "services/RedisService.hpp"

namespace disk::filters {

    /**
     * @brief 注册频率限制过滤器
     *
     * 实现基于 IP 地址的注册请求频率限制：
     * - 使用 Redis 固定窗口算法
     * - 默认限制：5 次/5分钟/IP
     * - 窗口大小：300 秒
     * - 仅对 /api/auth/register 路径生效
     * - 超过限制返回 429 Too Many Requests
     *
     * 响应头：
     * - X-RateLimit-Limit: 窗口内最大请求数
     * - X-RateLimit-Remaining: 窗口内剩余请求数
     * - X-RateLimit-Reset: 窗口重置时间（Unix 时间戳）
     * - Retry-After: 建议重试等待时间（秒）
     */
    class RegisterRateLimitFilter : public drogon::HttpCoroFilter<RegisterRateLimitFilter> {
    public:
        RegisterRateLimitFilter();

        [[nodiscard]]
        auto doFilter(const drogon::HttpRequestPtr& request)
            -> drogon::Task<drogon::HttpResponsePtr> override;

        static constexpr int DEFAULT_LIMIT = 5;
        static constexpr int WINDOW_SECONDS = 300;

    private:
        std::shared_ptr<disk::services::RedisService> m_redis_service{};

        [[nodiscard]]
        static auto GetCurrentWindow() -> int64_t {
            auto now = std::chrono::system_clock::now();
            auto timestamp =
                std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            return (timestamp / WINDOW_SECONDS) * WINDOW_SECONDS;
        }

        [[nodiscard]]
        static auto GetResetTime(int64_t window) -> int64_t {
            return window + WINDOW_SECONDS;
        }
    };

} // namespace disk::filters
