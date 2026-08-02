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
#include <array>
#include <cstddef>
#include <string_view>

#include <drogon/utils/coroutine.h>
#include <sodium/randombytes.h>

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

        [[nodiscard]]
        auto GenerateRequestId() -> std::string {
            std::array<unsigned char, 16> random_bytes{};
            randombytes_buf(random_bytes.data(), random_bytes.size());
            random_bytes[6] = static_cast<unsigned char>((random_bytes[6] & 0x0FU) | 0x40U);
            random_bytes[8] = static_cast<unsigned char>((random_bytes[8] & 0x3FU) | 0x80U);

            constexpr std::string_view HEX = "0123456789abcdef";
            std::array<char, 36> buffer{};
            std::size_t output_index = 0;
            for (std::size_t byte_index = 0; byte_index < random_bytes.size(); ++byte_index) {
                if (byte_index == 4 || byte_index == 6 || byte_index == 8 || byte_index == 10) {
                    buffer[output_index++] = '-';
                }
                const auto byte = random_bytes[byte_index];
                buffer[output_index++] = HEX[byte >> 4U];
                buffer[output_index++] = HEX[byte & 0x0FU];
            }

            return { buffer.data(), buffer.size() };
        }

        [[nodiscard]]
        auto IsValidRequestId(std::string_view request_id) noexcept -> bool {
            return !request_id.empty() && request_id.size() <= MAX_REQUEST_ID_LENGTH &&
                   std::ranges::all_of(request_id, IsAllowedRequestIdCharacter);
        }
    } // namespace

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
