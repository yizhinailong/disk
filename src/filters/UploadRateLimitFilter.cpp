/**
 * @file UploadRateLimitFilter.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 上传频率限制过滤器实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "UploadRateLimitFilter.hpp"
#include "filters/RateLimitHelper.hpp"

#include <algorithm>

#include "utils/ConfigMgr.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/RedisKeyPrefix.hpp"
#include "utils/Response.hpp"

namespace disk::filters {

    using disk::redis::RedisKeyPrefix;

    UploadRateLimitFilter::UploadRateLimitFilter()
        : m_redis_service(disk::services::RedisService::GetInstance()) {
        /// 初始化 RedisService 单例（如果尚未初始化）
        disk::services::RedisService::Initialize(drogon::app().getRedisClient());
    }

    auto UploadRateLimitFilter::doFilter(const drogon::HttpRequestPtr& request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        /// 从 request attributes 获取 user_id（由 JwtAuthFilter 设置）
        auto attrs = request->attributes();
        if (!attrs) {
            Logger::Warn() << "Cannot get request attributes";
            co_return nullptr;
        }

        if (!attrs->find("user_id")) {
            /// 没有 user_id，跳过频率限制（可能是 exempt 的路径）
            co_return nullptr;
        }

        const auto user_id = attrs->get<uint64_t>("user_id");
        const auto window = GetFixedWindowStart(WINDOW_SECONDS);
        const auto key = RedisKeyPrefix::BuildUploadRateLimitKey(user_id, window);
        const auto configured_limit =
            disk::utils::ConfigMgr::GetInstance()->GetUploadRateLimitPerMinute();
        const auto limit = configured_limit > 0 ? configured_limit : DEFAULT_LIMIT;

        /// 使用 Lua 脚本原子递增计数并设置过期时间（单次 Redis 交互）
        auto incr_result = co_await CheckFixedWindowLimit(m_redis_service, key, WINDOW_SECONDS);
        if (!incr_result) {
            Logger::Error() << "Redis IncrWithExpire failed: " << incr_result.error().message;
            /// Redis 失败时不阻止请求
            co_return nullptr;
        }

        const int64_t current_count = incr_result.value();

        /// 检查是否超过限制
        if (current_count > limit) {
            const auto reset_time = GetFixedWindowReset(window, WINDOW_SECONDS);
            Logger::Warn() << "Upload rate limit: user_id=" << user_id
                     << ", path=" << request->path()
                     << ", count=" << current_count;

            co_return BuildRateLimitExceededResponse(limit, reset_time);
        }

        Logger::Debug() << "Upload rate limit check passed: user_id=" << user_id
                  << ", count=" << current_count << "/" << limit;

        co_return nullptr;
    }

} ///< namespace disk::filters
