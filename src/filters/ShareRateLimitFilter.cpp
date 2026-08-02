/**
 * @file ShareRateLimitFilter.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Route-owned share operation rate-limit filters
 *
 * @copyright Copyright (c) 2026
 */

#include "ShareRateLimitFilter.hpp"

#include <optional>
#include <string_view>
#include <utility>

#include "filters/FilterLogContext.hpp"
#include "filters/RateLimitHelper.hpp"
#include "filters/ShareAuthFilter.hpp"
#include "services/RedisService.hpp"
#include "utils/ClientIp.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/RedisKeyPrefix.hpp"

namespace disk::filters {
    namespace {

        enum class ShareOperation {
            Browse,
            Download,
        };

        auto MakeRedisCounter() -> ShareRateLimitCounter {
            const auto redis_service = disk::services::RedisService::GetInstance();
            return [redis_service](
                       const std::string& key,
                       int window_seconds,
                       disk::utils::LogContext log_context
                   )
                       -> drogon::Task<Result<int64_t>> {
                co_return co_await CheckFixedWindowLimit(redis_service, key, window_seconds, log_context);
            };
        }

        auto ResolveOperation(std::string_view path) -> std::optional<ShareOperation> {
            if (path.starts_with("/api/share/browse/")) {
                return ShareOperation::Browse;
            }
            if (path.starts_with("/api/share/download/") ||
                path.starts_with("/api/share/save/")) {
                return ShareOperation::Download;
            }
            return std::nullopt;
        }

        auto OperationName(ShareOperation operation) -> std::string_view {
            return operation == ShareOperation::Browse ? "browse" : "download";
        }

    } // namespace

    ShareAccessRateLimitFilter::ShareAccessRateLimitFilter()
        : ShareAccessRateLimitFilter(MakeRedisCounter()) {
    }

    ShareAccessRateLimitFilter::ShareAccessRateLimitFilter(ShareRateLimitCounter counter)
        : m_counter(std::move(counter)) {
    }

    auto ShareAccessRateLimitFilter::doFilter(const drogon::HttpRequestPtr& request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        const auto log_context = GetFilterLogContext(request);
        if (!request->path().starts_with("/api/share/access/")) {
            co_return nullptr;
        }

        const auto config = disk::utils::ConfigMgr::GetInstance();
        const auto configured_window = config->GetShareAccessRateLimitWindowSeconds();
        const auto window_seconds =
            configured_window > 0 ? configured_window : DEFAULT_WINDOW_SECONDS;
        const auto configured_limit = config->GetShareAccessRateLimitPerMinute();
        const auto limit = configured_limit > 0 ? configured_limit : DEFAULT_LIMIT;
        const auto window = GetFixedWindowStart(window_seconds);
        const auto client_ip = disk::utils::ResolveClientIp(request);
        const auto key =
            disk::redis::RedisKeyPrefix::BuildShareAccessRateLimitKey(client_ip, window);

        auto count_result = co_await m_counter(key, window_seconds, log_context);
        if (!count_result) {
            Logger::Error(log_context)
                << "Share rate-limit counter failed: operation=access, client_ip="
                << client_ip << ", error=" << count_result.error().message;
            co_return nullptr;
        }

        if (*count_result > limit) {
            Logger::Warn(log_context)
                << "Share rate limit exceeded: operation=access, count=" << *count_result;
            co_return BuildRateLimitExceededResponse(
                limit,
                GetFixedWindowReset(window, window_seconds)
            );
        }

        co_return nullptr;
    }

    ShareOperationRateLimitFilter::ShareOperationRateLimitFilter()
        : ShareOperationRateLimitFilter(MakeRedisCounter()) {
    }

    ShareOperationRateLimitFilter::ShareOperationRateLimitFilter(ShareRateLimitCounter counter)
        : m_counter(std::move(counter)) {
    }

    auto ShareOperationRateLimitFilter::doFilter(const drogon::HttpRequestPtr& request)
        -> drogon::Task<drogon::HttpResponsePtr> {
        const auto log_context = GetFilterLogContext(request);
        const auto operation = ResolveOperation(request->path());
        if (!operation.has_value()) {
            co_return nullptr;
        }

        const auto attributes = request->attributes();
        if (!attributes || !attributes->find(ShareAuthFilter::SHARE_TOKEN_JTI_ATTRIBUTE)) {
            Logger::Error(log_context)
                << "Share rate-limit attribute missing: operation=" << OperationName(*operation);
            co_return nullptr;
        }

        const auto& jti =
            attributes->get<std::string>(ShareAuthFilter::SHARE_TOKEN_JTI_ATTRIBUTE);
        if (jti.empty()) {
            Logger::Error(log_context)
                << "Share rate-limit attribute empty: operation=" << OperationName(*operation);
            co_return nullptr;
        }

        const auto config = disk::utils::ConfigMgr::GetInstance();
        const bool is_browse = *operation == ShareOperation::Browse;
        const auto configured_window = is_browse ?
                                           config->GetShareBrowseRateLimitWindowSeconds() :
                                           config->GetShareDownloadRateLimitWindowSeconds();
        const auto default_window = is_browse ?
                                        DEFAULT_BROWSE_WINDOW_SECONDS :
                                        DEFAULT_DOWNLOAD_WINDOW_SECONDS;
        const auto window_seconds =
            configured_window > 0 ? configured_window : default_window;
        const auto configured_limit = is_browse ?
                                          config->GetShareBrowseRateLimitPerMinute() :
                                          config->GetShareDownloadRateLimitPerMinute();
        const auto default_limit =
            is_browse ? DEFAULT_BROWSE_LIMIT : DEFAULT_DOWNLOAD_LIMIT;
        const auto limit = configured_limit > 0 ? configured_limit : default_limit;
        const auto window = GetFixedWindowStart(window_seconds);
        const auto key = is_browse ?
                             disk::redis::RedisKeyPrefix::BuildShareBrowseRateLimitKey(jti, window) :
                             disk::redis::RedisKeyPrefix::BuildShareDownloadRateLimitKey(jti, window);

        auto count_result = co_await m_counter(key, window_seconds, log_context);
        if (!count_result) {
            Logger::Error(log_context)
                << "Share rate-limit counter failed: operation=" << OperationName(*operation)
                << ", error=" << count_result.error().message;
            co_return nullptr;
        }

        if (*count_result > limit) {
            Logger::Warn(log_context)
                << "Share rate limit exceeded: operation=" << OperationName(*operation)
                << ", count=" << *count_result;
            co_return BuildRateLimitExceededResponse(
                limit,
                GetFixedWindowReset(window, window_seconds)
            );
        }

        co_return nullptr;
    }

} // namespace disk::filters
