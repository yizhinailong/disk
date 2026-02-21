/**
 * @file UploadRateLimitFilter.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 上传频率限制过滤器实现
 * @version 0.1
 * @date 2026-02-18
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "UploadRateLimitFilter.hpp"

#include "utils/ErrorCode.hpp"
#include "utils/RedisKeyPrefix.hpp"
#include "utils/Response.hpp"

namespace disk::filters {

    using disk::redis::RedisKeyPrefix;

    UploadRateLimitFilter::UploadRateLimitFilter()
        : m_redis_service(
              std::make_unique<disk::services::RedisService>(drogon::app().getRedisClient())
          ) {
        LOG_DEBUG << "UploadRateLimitFilter initialized";
    }

    auto UploadRateLimitFilter::doFilter(const drogon::HttpRequestPtr& request)
        -> drogon::Task<drogon::HttpResponsePtr> {

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
        const auto key = RedisKeyPrefix::BuildUploadRateLimitKey(user_id, window);

        // 尝试获取当前计数
        auto get_result = co_await m_redis_service->Get(key);

        int64_t current_count = 0;
        if (get_result) {
            try {
                current_count = std::stoll(get_result.value());
            } catch (const std::exception& e) {
                LOG_ERROR << "Failed to parse count: " << e.what();
                current_count = 0;
            }
        }

        if (current_count >= DEFAULT_LIMIT) {
            LOG_WARN << "Upload rate limit: user_id=" << user_id << ", count=" << current_count;

            auto response = disk::Response::Error(disk::error::Code::TooManyRequests);
            response->addHeader("X-RateLimit-Limit", std::to_string(DEFAULT_LIMIT));
            response->addHeader("X-RateLimit-Remaining", "0");
            response->addHeader("X-RateLimit-Reset", std::to_string(GetResetTime(window)));

            co_return response;
        }

        // 原子递增计数
        auto incr_result = co_await m_redis_service->Incr(key);
        if (!incr_result) {
            LOG_ERROR << "Redis increment failed: " << incr_result.error().message;
            // Redis 失败时不阻止请求
            co_return nullptr;
        }

        // 如果是第一次请求，设置过期时间
        if (incr_result.value() == 1) {
            co_await m_redis_service->Expire(key, WINDOW_SECONDS);
        }

        // 添加响应头（可选，供客户端参考）
        // 注意：这里不能直接设置响应头，因为响应还没生成
        // 可以在 request attributes 中存储，让后续处理添加

        LOG_DEBUG << "Upload rate limit check passed: user_id=" << user_id
                  << ", count=" << incr_result.value() << "/" << DEFAULT_LIMIT;

        co_return nullptr;
    }

} // namespace disk::filters
