/**
 * @file RedisKeyPrefix.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Redis key prefix construction utility class
 * @version 0.1
 * @date 2026-01-28
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

namespace disk::redis {

    /**
     * @brief Redis key prefix construction utility class
     *
     * Centralizes Redis key construction patterns:
     * - Refresh tokens: `refresh_token:{user_id}`
     * - Access token blacklist: `access_token_blacklist:{jti}`
     * - Login rate limiting: `rate:login:{ip_address}`
     *
     * All IP addresses are normalized by removing port numbers:
     * - IPv4: "192.168.1.1:8080" → "192.168.1.1"
     * - IPv6 with brackets: "[2001:db8::1]:8080" → "2001:db8::1"
     * - IPv6 without brackets: "2001:db8::1" → "2001:db8::1"
     */
    class RedisKeyPrefix {
    public:
        // ==================== Constants ====================
        static constexpr std::string_view REFRESH_TOKEN_PREFIX = "refresh_token";
        static constexpr std::string_view ACCESS_TOKEN_BLACKLIST_PREFIX = "access_token_blacklist";
        static constexpr std::string_view LOGIN_RATE_LIMIT_PREFIX = "rate:login";

        // ==================== Key Construction Methods ====================

        /**
         * @brief Build refresh token key
         *
         * @param user_id User ID
         * @return std::string Redis key format: "refresh_token:{user_id}"
         */
        [[nodiscard]]
        static auto BuildRefreshTokenKey(uint64_t user_id) -> std::string {
            return std::string(REFRESH_TOKEN_PREFIX) + ":" + std::to_string(user_id);
        }

        /**
         * @brief Build access token blacklist key
         *
         * @param jti JWT token ID
         * @return std::string Redis key format: "access_token_blacklist:{jti}"
         */
        [[nodiscard]]
        static auto BuildAccessTokenBlacklistKey(const std::string& jti) -> std::string {
            return std::string(ACCESS_TOKEN_BLACKLIST_PREFIX) + ":" + jti;
        }

        /**
         * @brief Build login rate limit key
         *
         * @param ip_address IP address (with optional port)
         * @return std::string Redis key format: "rate:login:{normalized_ip}"
         */
        [[nodiscard]]
        static auto BuildLoginRateLimitKey(const std::string& ip_address) -> std::string {
            return std::string(LOGIN_RATE_LIMIT_PREFIX) + ":" + ExtractIPOnly(ip_address);
        }

        /**
         * @brief Extract IP address only (remove port)
         *
         * Handles different IP formats:
         * - IPv4: "192.168.1.1:8080" → "192.168.1.1"
         * - IPv6 with brackets: "[2001:db8::1]:8080" → "2001:db8::1"
         * - IPv6 without brackets and without port: "2001:db8::1" → "2001:db8::1"
         * - IPv6 without brackets but with port: "[::1]:8080" → "::1"
         *
         * @param ip_address IP address string (may include port)
         * @return std::string IP address without port
         */
        [[nodiscard]]
        static auto ExtractIPOnly(const std::string& ip_address) -> std::string {
            // Handle IPv6 with brackets: "[2001:db8::1]:8080"
            if (!ip_address.empty() && ip_address.front() == '[') {
                auto closing_bracket = ip_address.find(']');
                if (closing_bracket != std::string::npos) {
                    return ip_address.substr(1, closing_bracket - 1);
                }
            }

            // Check if this is likely an IPv6 address (contains multiple colons)
            auto colon_count = std::count(ip_address.begin(), ip_address.end(), ':');
            if (colon_count > 1) {
                // This is likely an IPv6 address, return as-is (no port removal)
                return ip_address;
            }

            // Handle IPv4 and simple IPv6 without brackets: "192.168.1.1:8080"
            auto colon_pos = ip_address.find(':');
            if (colon_pos != std::string::npos) {
                return ip_address.substr(0, colon_pos);
            }

            // No port found, return as-is
            return ip_address;
        }
    };

} // namespace disk::redis
