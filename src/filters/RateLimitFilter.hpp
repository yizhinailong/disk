/**
 * @file RateLimitFilter.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 回收站路由通用用户频率限制过滤器
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

    using ApiRateLimitCounter =
        std::function<drogon::Task<Result<int64_t>>(
            const std::string&,
            int,
            disk::utils::LogContext
        )>;

    /**
     * @brief 回收站路由通用用户频率限制过滤器
     *
     * 实现基于用户 ID 的回收站请求频率限制：
     * - 使用 Redis 固定窗口算法
     * - 默认限制：100 次/分钟/用户
     * - 窗口大小：60 秒
     * - 只由 TrashController 的五条路由声明
     * - 超过限制返回 429 Too Many Requests
     *
     * 响应头：
     * - X-RateLimit-Limit: 窗口内最大请求数
     * - X-RateLimit-Remaining: 窗口内剩余请求数
     * - X-RateLimit-Reset: 窗口重置时间（Unix 时间戳）
     */
    class RateLimitFilter : public drogon::HttpCoroFilter<RateLimitFilter> {
    public:
        RateLimitFilter();
        explicit RateLimitFilter(ApiRateLimitCounter counter);

        [[nodiscard]]
        auto doFilter(const drogon::HttpRequestPtr& request)
            -> drogon::Task<drogon::HttpResponsePtr> override;

        static constexpr int DEFAULT_LIMIT = 100;
        static constexpr int WINDOW_SECONDS = 60;

    private:
        ApiRateLimitCounter m_counter;
    };

} // namespace disk::filters
