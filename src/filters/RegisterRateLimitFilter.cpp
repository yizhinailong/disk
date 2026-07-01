/**
 * @file RegisterRateLimitFilter.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 注册频率限制过滤器实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "RegisterRateLimitFilter.hpp"
#include "filters/RateLimitHelper.hpp"

#include <algorithm>

#include "utils/ConfigMgr.hpp"
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

        /// 仅对注册路径生效
        const auto& path = request->path();
        if (path != "/api/auth/register") {
            co_return nullptr;
        }

        /// 从请求获取 IP 地址
        const auto ip = RedisKeyPrefix::ExtractIPOnly(request->peerAddr().toIp());
        const auto config = disk::utils::ConfigMgr::GetInstance();
        const auto configured_window = config->GetRegisterRateLimitWindowSeconds();
        const auto window_seconds = configured_window > 0 ? configured_window : WINDOW_SECONDS;
        const auto window = GetFixedWindowStart(window_seconds);
        const auto key =
            std::string("rate:register:") + ip + ":" + std::to_string(window);
        const auto configured_limit = config->GetRegisterRateLimitPerWindow();
        const auto limit = configured_limit > 0 ? configured_limit : DEFAULT_LIMIT;

        auto incr_result = co_await CheckFixedWindowLimit(m_redis_service, key, window_seconds);
        if (!incr_result) {
            Logger::Error() << "Redis IncrWithExpire failed: " << incr_result.error().message;
            co_return nullptr;
        }

        const int64_t current_count = incr_result.value();

        if (current_count > limit) {
            const auto reset_time = GetFixedWindowReset(window, window_seconds);
            Logger::Warn() << "Register rate limit: ip=" << ip
                     << ", count=" << current_count;

            co_return BuildRateLimitExceededResponse(limit, reset_time);
        }

        Logger::Debug() << "Register rate limit check passed: ip=" << ip
                  << ", count=" << current_count << "/" << limit;

        co_return nullptr;
    }

} ///< namespace disk::filters
