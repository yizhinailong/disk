/**
 * @file RedisKeyPrefix_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Redis key prefix construction tests
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "utils/RedisKeyPrefix.hpp"

#include <string>

#include <gtest/gtest.h>

using disk::redis::RedisKeyPrefix;

TEST(RedisKeyPrefix, BuildRefreshTokenKeyValidUserId) {
    uint64_t user_id = 12345;
    auto result = RedisKeyPrefix::BuildRefreshTokenKey(user_id);
    EXPECT_EQ(result, "refresh_token:12345");
}

TEST(RedisKeyPrefix, BuildRefreshTokenKeyZeroUserId) {
    uint64_t user_id = 0;
    auto result = RedisKeyPrefix::BuildRefreshTokenKey(user_id);
    EXPECT_EQ(result, "refresh_token:0");
}

TEST(RedisKeyPrefix, BuildRefreshTokenKeyMaxUserId) {
    uint64_t user_id = std::numeric_limits<uint64_t>::max();
    auto result = RedisKeyPrefix::BuildRefreshTokenKey(user_id);
    EXPECT_EQ(result, "refresh_token:18446744073709551615");
}

TEST(RedisKeyPrefix, BuildAccessTokenBlacklistKeyValidJti) {
    std::string jti = "abc123def456";
    auto result = RedisKeyPrefix::BuildAccessTokenBlacklistKey(jti);
    EXPECT_EQ(result, "access_token_blacklist:abc123def456");
}

TEST(RedisKeyPrefix, BuildAccessTokenBlacklistKeyEmptyJti) {
    std::string jti;
    auto result = RedisKeyPrefix::BuildAccessTokenBlacklistKey(jti);
    EXPECT_EQ(result, "access_token_blacklist:");
}

TEST(RedisKeyPrefix, BuildAccessTokenBlacklistKeyLongJti) {
    std::string jti(100, 'a');
    auto result = RedisKeyPrefix::BuildAccessTokenBlacklistKey(jti);
    EXPECT_EQ(result, "access_token_blacklist:" + std::string(100, 'a'));
}

TEST(RedisKeyPrefix, BuildLoginRateLimitKeyValidIPv4) {
    std::string ip = "192.168.1.1";
    auto result = RedisKeyPrefix::BuildLoginRateLimitKey(ip);
    EXPECT_EQ(result, "rate:login:192.168.1.1");
}

TEST(RedisKeyPrefix, BuildLoginRateLimitKeyValidIPv6) {
    std::string ip = "2001:db8::1";
    auto result = RedisKeyPrefix::BuildLoginRateLimitKey(ip);
    EXPECT_EQ(result, "rate:login:2001:db8::1");
}

TEST(RedisKeyPrefix, BuildLoginRateLimitKeyWithPort) {
    std::string ip = "192.168.1.1:8080";
    auto result = RedisKeyPrefix::BuildLoginRateLimitKey(ip);
    EXPECT_EQ(result, "rate:login:192.168.1.1");
}

TEST(RedisKeyPrefix, BuildLoginRateLimitKeyIPv6WithPort) {
    std::string ip = "[2001:db8::1]:8080";
    auto result = RedisKeyPrefix::BuildLoginRateLimitKey(ip);
    EXPECT_EQ(result, "rate:login:2001:db8::1");
}

TEST(RedisKeyPrefix, BuildFileListCacheKeyIncludesPageSize) {
    auto result = RedisKeyPrefix::BuildFileListCacheKey(42, 7, "all", "name", "asc", 3, 50);
    EXPECT_EQ(result, "file_list:42:7:all:name:asc:3:50");
}

TEST(RedisKeyPrefix, BuildFileListCacheKeySeparatesDifferentPageSizes) {
    auto default_page = RedisKeyPrefix::BuildFileListCacheKey(42, 7, "all", "name", "asc", 1, 20);
    auto custom_page = RedisKeyPrefix::BuildFileListCacheKey(42, 7, "all", "name", "asc", 1, 37);
    EXPECT_NE(default_page, custom_page);
    EXPECT_EQ(custom_page, "file_list:42:7:all:name:asc:1:37");
}

TEST(RedisKeyPrefix, BuildFileListCachePrefix) {
    auto result = RedisKeyPrefix::BuildFileListCachePrefix(42, 7);
    EXPECT_EQ(result, "file_list:42:7:");
}

/// ==================== Share Token Redis Key Tests ====================

TEST(RedisKeyPrefix, BuildShareTokenKeyValidShareCodeAndHash) {
    std::string share_code = "AbCd1234";
    std::string token_hash = "a1b2c3d4e5f6";
    auto result = RedisKeyPrefix::BuildShareTokenKey(share_code, token_hash);
    EXPECT_EQ(result, "share_token:AbCd1234:a1b2c3d4e5f6");
}

TEST(RedisKeyPrefix, BuildShareTokenKeyEmptyTokenHash) {
    std::string share_code = "XyZ999";
    std::string token_hash;
    auto result = RedisKeyPrefix::BuildShareTokenKey(share_code, token_hash);
    EXPECT_EQ(result, "share_token:XyZ999:");
}

TEST(RedisKeyPrefix, BuildShareTokenKeyEmptyShareCode) {
    std::string share_code;
    std::string token_hash = "hash123";
    auto result = RedisKeyPrefix::BuildShareTokenKey(share_code, token_hash);
    EXPECT_EQ(result, "share_token::hash123");
}

TEST(RedisKeyPrefix, BuildShareTokenBlacklistKeyValidHash) {
    std::string token_hash = "revoked_token_hash_abc";
    auto result = RedisKeyPrefix::BuildShareTokenBlacklistKey(token_hash);
    EXPECT_EQ(result, "share_token_blacklist:revoked_token_hash_abc");
}

TEST(RedisKeyPrefix, BuildShareTokenBlacklistKeyEmptyHash) {
    std::string token_hash;
    auto result = RedisKeyPrefix::BuildShareTokenBlacklistKey(token_hash);
    EXPECT_EQ(result, "share_token_blacklist:");
}

TEST(RedisKeyPrefix, BuildSharePasswordRateLimitKeyValidShareCodeAndIP) {
    std::string share_code = "AbCd12";
    std::string ip = "192.168.1.100";
    auto result = RedisKeyPrefix::BuildSharePasswordRateLimitKey(share_code, ip);
    EXPECT_EQ(result, "rate:share_password:AbCd12:192.168.1.100");
}

TEST(RedisKeyPrefix, BuildSharePasswordRateLimitKeyIPWithPort) {
    std::string share_code = "XyZ99";
    std::string ip = "10.0.0.1:8080";
    auto result = RedisKeyPrefix::BuildSharePasswordRateLimitKey(share_code, ip);
    EXPECT_EQ(result, "rate:share_password:XyZ99:10.0.0.1");
}

TEST(RedisKeyPrefix, BuildSharePasswordRateLimitKeyIPv6WithPort) {
    std::string share_code = "Test01";
    std::string ip = "[2001:db8::1]:9090";
    auto result = RedisKeyPrefix::BuildSharePasswordRateLimitKey(share_code, ip);
    EXPECT_EQ(result, "rate:share_password:Test01:2001:db8::1");
}

TEST(RedisKeyPrefix, BuildShareAccessRateLimitKeyNormalizesIPv4Port) {
    const auto result =
        RedisKeyPrefix::BuildShareAccessRateLimitKey("192.168.1.100:8080", 120);
    EXPECT_EQ(result, "rate:share_access:192.168.1.100:120");
}

TEST(RedisKeyPrefix, BuildShareAccessRateLimitKeyNormalizesIPv6Port) {
    const auto result =
        RedisKeyPrefix::BuildShareAccessRateLimitKey("[2001:db8::1]:8080", 180);
    EXPECT_EQ(result, "rate:share_access:2001:db8::1:180");
}

TEST(RedisKeyPrefix, BuildShareRateLimitKeysSeparateOperations) {
    const auto browse = RedisKeyPrefix::BuildShareBrowseRateLimitKey("jti-123", 240);
    const auto download = RedisKeyPrefix::BuildShareDownloadRateLimitKey("jti-123", 240);

    EXPECT_EQ(browse, "rate:share_browse:jti-123:240");
    EXPECT_EQ(download, "rate:share_download:jti-123:240");
    EXPECT_NE(browse, download);
}

TEST(RedisKeyPrefix, BuildShareRateLimitKeysSeparateJtisAndWindows) {
    const auto first_jti = RedisKeyPrefix::BuildShareBrowseRateLimitKey("jti-123", 240);
    const auto second_jti = RedisKeyPrefix::BuildShareBrowseRateLimitKey("jti-456", 240);
    const auto next_window = RedisKeyPrefix::BuildShareBrowseRateLimitKey("jti-123", 300);

    EXPECT_NE(first_jti, second_jti);
    EXPECT_NE(first_jti, next_window);
}
