/**
 * @file ShareRateLimitFilter.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Route-owned share operation rate-limit filters
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <drogon/HttpFilter.h>

#include "utils/ErrorCode.hpp"

namespace disk::filters {

    using ShareRateLimitCounter =
        std::function<drogon::Task<Result<int64_t>>(const std::string&, int)>;

    class ShareAccessRateLimitFilter : public drogon::HttpCoroFilter<ShareAccessRateLimitFilter> {
    public:
        ShareAccessRateLimitFilter();
        explicit ShareAccessRateLimitFilter(ShareRateLimitCounter counter);

        [[nodiscard]]
        auto doFilter(const drogon::HttpRequestPtr& request)
            -> drogon::Task<drogon::HttpResponsePtr> override;

        static constexpr int DEFAULT_LIMIT = 30;
        static constexpr int DEFAULT_WINDOW_SECONDS = 60;

    private:
        ShareRateLimitCounter m_counter;
    };

    class ShareOperationRateLimitFilter : public drogon::HttpCoroFilter<ShareOperationRateLimitFilter> {
    public:
        ShareOperationRateLimitFilter();
        explicit ShareOperationRateLimitFilter(ShareRateLimitCounter counter);

        [[nodiscard]]
        auto doFilter(const drogon::HttpRequestPtr& request)
            -> drogon::Task<drogon::HttpResponsePtr> override;

        static constexpr int DEFAULT_BROWSE_LIMIT = 60;
        static constexpr int DEFAULT_BROWSE_WINDOW_SECONDS = 60;
        static constexpr int DEFAULT_DOWNLOAD_LIMIT = 10;
        static constexpr int DEFAULT_DOWNLOAD_WINDOW_SECONDS = 60;

    private:
        ShareRateLimitCounter m_counter;
    };

} // namespace disk::filters
