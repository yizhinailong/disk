/**
 * @file SharePublicRateLimitFilter.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 分享公开接口频率限制过滤器实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "SharePublicRateLimitFilter.hpp"
#include "filters/RateLimitHelper.hpp"

#include <algorithm>

#include "utils/ErrorCode.hpp"
#include "utils/RedisKeyPrefix.hpp"
#include "utils/Response.hpp"

namespace disk::filters {

    using disk::redis::RedisKeyPrefix;

    SharePublicRateLimitFilter::SharePublicRateLimitFilter()
        : m_redis_service(disk::services::RedisService::GetInstance()) {
    }

    auto SharePublicRateLimitFilter::doFilter(const drogon::HttpRequestPtr& request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        const auto& path = request->path();
        if (!IsSharePublicPath(path)) {
            co_return nullptr;
        }

        const auto ip = RedisKeyPrefix::ExtractIPOnly(request->peerAddr().toIp());
        const auto window = GetFixedWindowStart(WINDOW_SECONDS);
        const auto key =
            std::string("rate:share_public:") + ip + ":" + std::to_string(window);

        auto incr_result = co_await CheckFixedWindowLimit(m_redis_service, key, WINDOW_SECONDS);
        if (!incr_result) {
            Logger::Error() << "Redis IncrWithExpire failed: " << incr_result.error().message;
            co_return nullptr;
        }

        const int64_t current_count = incr_result.value();

        if (current_count > DEFAULT_LIMIT) {
            const auto reset_time = GetFixedWindowReset(window, WINDOW_SECONDS);
            Logger::Warn() << "Share public rate limit: ip=" << ip
                     << ", path=" << path
                     << ", count=" << current_count;

            co_return BuildRateLimitExceededResponse(DEFAULT_LIMIT, reset_time);
        }

        Logger::Debug() << "Share public rate limit check passed: ip=" << ip
                  << ", count=" << current_count << "/" << DEFAULT_LIMIT;

        co_return nullptr;
    }

} ///< namespace disk::filters
