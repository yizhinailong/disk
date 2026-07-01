/**
 * @file RedisKeyPrefix.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Redis key prefix construction utility class
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
        /// ==================== 常量 ====================
        static constexpr std::string_view REFRESH_TOKEN_PREFIX = "refresh_token";
        static constexpr std::string_view ACCESS_TOKEN_BLACKLIST_PREFIX = "access_token_blacklist";
        static constexpr std::string_view LOGIN_RATE_LIMIT_PREFIX = "rate:login";
        static constexpr std::string_view SHARE_TOKEN_PREFIX = "share_token";
        static constexpr std::string_view SHARE_TOKEN_BLACKLIST_PREFIX = "share_token_blacklist";
        static constexpr std::string_view SHARE_PASSWORD_RATE_LIMIT_PREFIX = "rate:share_password";
        static constexpr std::string_view API_RATE_LIMIT_PREFIX = "rate:api";
        static constexpr std::string_view UPLOAD_RATE_LIMIT_PREFIX = "rate:upload";
        static constexpr std::string_view FILE_LIST_CACHE_PREFIX = "file_list";

        /// ==================== 键构造方法 ====================

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
         * @brief Build share token key
         *
         * @param share_code Share code (external identifier)
         * @param token_hash SHA256 hash of the share token
         * @return std::string Redis key format: "share_token:{share_code}:{token_hash}"
         */
        [[nodiscard]]
        static auto BuildShareTokenKey(const std::string& share_code, const std::string& token_hash)
            -> std::string {
            return std::string(SHARE_TOKEN_PREFIX) + ":" + share_code + ":" + token_hash;
        }

        /**
         * @brief Build share token blacklist key
         *
         * @param token_hash SHA256 hash of the revoked share token
         * @return std::string Redis key format: "share_token_blacklist:{token_hash}"
         */
        [[nodiscard]]
        static auto BuildShareTokenBlacklistKey(const std::string& token_hash) -> std::string {
            return std::string(SHARE_TOKEN_BLACKLIST_PREFIX) + ":" + token_hash;
        }

        /**
         * @brief Build share password rate limit key
         *
         * @param share_code Share code
         * @param ip_address IP address (with optional port)
         * @return std::string Redis key format: "rate:share_password:{share_code}:{normalized_ip}"
         */
        [[nodiscard]]
        static auto
        BuildSharePasswordRateLimitKey(const std::string& share_code, const std::string& ip_address)
            -> std::string {
            return std::string(SHARE_PASSWORD_RATE_LIMIT_PREFIX) + ":" + share_code + ":" +
                   ExtractIPOnly(ip_address);
        }

        /**
         * @brief Build API rate limit key
         *
         * @param user_id User ID
         * @param window_timestamp Window timestamp (Unix timestamp in seconds, rounded to window)
         * @return std::string Redis key format: "rate:api:{user_id}:{window_timestamp}"
         */
        [[nodiscard]]
        static auto BuildApiRateLimitKey(uint64_t user_id, int64_t window_timestamp)
            -> std::string {
            return std::string(API_RATE_LIMIT_PREFIX) + ":" + std::to_string(user_id) + ":" +
                   std::to_string(window_timestamp);
        }

        /**
         * @brief Build upload rate limit key
         *
         * @param user_id User ID
         * @param window_timestamp Window timestamp (Unix timestamp in seconds, rounded to window)
         * @return std::string Redis key format: "rate:upload:{user_id}:{window_timestamp}"
         */
        [[nodiscard]]
        static auto BuildUploadRateLimitKey(uint64_t user_id, int64_t window_timestamp)
            -> std::string {
            return std::string(UPLOAD_RATE_LIMIT_PREFIX) + ":" + std::to_string(user_id) + ":" +
                   std::to_string(window_timestamp);
        }

        /**
         * @brief Build file list cache key
         *
         * @param user_id User ID
         * @param parent_id Parent folder ID
         * @param type Item type filter (all/file/folder)
         * @param sort_by Sort field name
         * @param sort_order Sort direction (asc/desc)
         * @param page Page number
         * @param page_size Page size
         * @return std::string Redis key format: "file_list:{user_id}:{parent_id}:{type}:{sort_by}:{sort_order}:{page}:{page_size}"
         */
        [[nodiscard]]
        static auto BuildFileListCacheKey(
            uint64_t user_id,
            uint64_t parent_id,
            const std::string& type,
            const std::string& sort_by,
            const std::string& sort_order,
            int page,
            int page_size
        ) -> std::string {
            return std::string(FILE_LIST_CACHE_PREFIX) + ":" +
                   std::to_string(user_id) + ":" +
                   std::to_string(parent_id) + ":" +
                   type + ":" +
                   sort_by + ":" +
                   sort_order + ":" +
                   std::to_string(page) + ":" +
                   std::to_string(page_size);
        }

        /**
         * @brief Build file list cache prefix key for pattern-based invalidation
         *
         * @param user_id User ID
         * @param parent_id Parent folder ID
         * @return std::string Redis key prefix: "file_list:{user_id}:{parent_id}:"
         */
        [[nodiscard]]
        static auto BuildFileListCachePrefix(
            uint64_t user_id,
            uint64_t parent_id
        ) -> std::string {
            return std::string(FILE_LIST_CACHE_PREFIX) + ":" +
                   std::to_string(user_id) + ":" +
                   std::to_string(parent_id) + ":";
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
            /// 处理带方括号的 IPv6 地址："[2001:db8::1]:8080"
            if (!ip_address.empty() && ip_address.front() == '[') {
                auto closing_bracket = ip_address.find(']');
                if (closing_bracket != std::string::npos) {
                    return ip_address.substr(1, closing_bracket - 1);
                }
            }

            /// 检查是否可能是 IPv6 地址（包含多个冒号）
            auto colon_count = std::count(ip_address.begin(), ip_address.end(), ':');
            if (colon_count > 1) {
                /// 这可能是 IPv6 地址，原样返回（不移除端口）
                return ip_address;
            }

            /// 处理 IPv4 和不带方括号的简单 IPv6：如 "192.168.1.1:8080"
            auto colon_pos = ip_address.find(':');
            if (colon_pos != std::string::npos) {
                return ip_address.substr(0, colon_pos);
            }

            /// 未找到端口，原样返回
            return ip_address;
        }
    };

} ///< namespace disk::redis
