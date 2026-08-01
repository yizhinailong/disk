/**
 * @file RegisterRateLimitFilter.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 注册频率限制过滤器
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <drogon/HttpFilter.h>

#include "utils/ErrorCode.hpp"
#include "utils/LogHelper.hpp"

namespace disk::filters {

    using RegisterRateLimitCounter =
        std::function<drogon::Task<Result<int64_t>>(
            const std::string&,
            int,
            disk::utils::LogContext
        )>;

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
        explicit RegisterRateLimitFilter(RegisterRateLimitCounter counter);

        [[nodiscard]]
        auto doFilter(const drogon::HttpRequestPtr& request)
            -> drogon::Task<drogon::HttpResponsePtr> override;

        static constexpr int DEFAULT_LIMIT = 5;
        static constexpr int WINDOW_SECONDS = 300;

    private:
        RegisterRateLimitCounter m_counter;
    };

} // namespace disk::filters
