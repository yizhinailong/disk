/**
 * @file RequestTraceFilter.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 请求追踪过滤器实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "RequestTraceFilter.hpp"

#include <algorithm>
#include <cstddef>
#include <random>

#include <drogon/utils/coroutine.h>

namespace disk::filters {

    namespace {
        constexpr std::size_t MAX_REQUEST_ID_LENGTH = 128;

        [[nodiscard]]
        auto IsAllowedRequestIdCharacter(char character) noexcept -> bool {
            return (character >= 'a' && character <= 'z') ||
                   (character >= 'A' && character <= 'Z') ||
                   (character >= '0' && character <= '9') || character == '.' ||
                   character == '_' || character == ':' || character == '-';
        }
    } // namespace

    auto RequestTraceFilter::GenerateRequestId() -> std::string {
        /// UUID v4: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
        thread_local std::random_device rd;
        thread_local std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> dist(0, 15);
        std::uniform_int_distribution<uint32_t> dist_y(8, 11);

        constexpr const char* hex = "0123456789abcdef";
        char buf[36];
        for (int i = 0; i < 8; ++i) {
            buf[i] = hex[dist(gen)];
        }
        buf[8] = '-';
        for (int i = 9; i < 13; ++i) {
            buf[i] = hex[dist(gen)];
        }
        buf[13] = '-';
        buf[14] = '4'; ///< version 4
        for (int i = 15; i < 18; ++i) {
            buf[i] = hex[dist(gen)];
        }
        buf[18] = '-';
        buf[19] = hex[dist_y(gen)]; ///< variant: 8/9/a/b
        for (int i = 20; i < 23; ++i) {
            buf[i] = hex[dist(gen)];
        }
        buf[23] = '-';
        for (int i = 24; i < 36; ++i) {
            buf[i] = hex[dist(gen)];
        }

        return { buf, 36 };
    }

    auto RequestTraceFilter::IsValidRequestId(std::string_view request_id) noexcept -> bool {
        return !request_id.empty() && request_id.size() <= MAX_REQUEST_ID_LENGTH &&
               std::ranges::all_of(request_id, IsAllowedRequestIdCharacter);
    }

    auto RequestTraceFilter::ResolveRequestId(
        const drogon::HttpRequestPtr& request
    ) -> std::string {
        const auto incoming_request_id = request->getHeader("X-Request-Id");
        if (IsValidRequestId(incoming_request_id)) {
            return incoming_request_id;
        }
        return GenerateRequestId();
    }

    auto RequestTraceFilter::doFilter(const drogon::HttpRequestPtr& request)
        -> drogon::Task<drogon::HttpResponsePtr> {

        if (!request->attributes()->find("request_id")) {
            auto request_id = ResolveRequestId(request);
            request->attributes()->insert("request_id", std::move(request_id));
        }

        co_return nullptr;
    }

} // namespace disk::filters
