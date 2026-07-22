/**
 * @file RateLimitHelper.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 固定窗口限流通用辅助函数
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include <drogon/drogon.h>

#include "services/RedisService.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/LogHelper.hpp"
#include "utils/Response.hpp"

namespace disk::filters {

    [[nodiscard]]
    inline auto GetFixedWindowStart(int64_t window_seconds) -> int64_t {
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                             now.time_since_epoch()
        )
                             .count();
        return (timestamp / window_seconds) * window_seconds;
    }

    [[nodiscard]]
    inline auto GetFixedWindowReset(int64_t window, int64_t window_seconds) -> int64_t {
        return window + window_seconds;
    }

    [[nodiscard]]
    inline auto BuildRateLimitExceededResponse(
        int64_t limit,
        int64_t reset_time,
        bool include_retry_after = true
    ) -> drogon::HttpResponsePtr {
        auto response = disk::Response::Error(disk::error::Code::TooManyRequests);
        response->addHeader("X-RateLimit-Limit", std::to_string(limit));
        response->addHeader("X-RateLimit-Remaining", "0");
        response->addHeader("X-RateLimit-Reset", std::to_string(reset_time));

        if (include_retry_after) {
            const auto now = std::chrono::system_clock::now();
            const auto now_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                                         now.time_since_epoch()
            )
                                         .count();
            const auto retry_after = std::max<int64_t>(1, reset_time - now_seconds);
            response->addHeader("Retry-After", std::to_string(retry_after));
        }

        return response;
    }

    [[nodiscard]]
    inline auto CheckFixedWindowLimit(
        const std::shared_ptr<disk::services::RedisService>& redis_service,
        const std::string& key,
        int64_t window_seconds,
        disk::utils::LogContext log_context
    ) -> drogon::Task<Result<int64_t>> {
        auto incr_result =
            co_await redis_service->IncrWithExpire(key, window_seconds, log_context);
        if (!incr_result) {
            co_return std::unexpected(incr_result.error());
        }

        co_return incr_result.value();
    }

} // namespace disk::filters
