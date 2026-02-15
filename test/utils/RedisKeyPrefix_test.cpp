/**
 * @file RedisKeyPrefix_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Redis key prefix construction tests
 * @version 0.1
 * @date 2026-01-28
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "utils/RedisKeyPrefix.hpp"

#include <string>

#include <gtest/gtest.h>

using disk::redis::RedisKeyPrefix;

TEST(RedisKeyPrefix, BuildRefreshTokenKey_ValidUserId) {
    uint64_t user_id = 12345;
    auto result = RedisKeyPrefix::BuildRefreshTokenKey(user_id);
    EXPECT_EQ(result, "refresh_token:12345");
}

TEST(RedisKeyPrefix, BuildRefreshTokenKey_ZeroUserId) {
    uint64_t user_id = 0;
    auto result = RedisKeyPrefix::BuildRefreshTokenKey(user_id);
    EXPECT_EQ(result, "refresh_token:0");
}

TEST(RedisKeyPrefix, BuildRefreshTokenKey_MaxUserId) {
    uint64_t user_id = std::numeric_limits<uint64_t>::max();
    auto result = RedisKeyPrefix::BuildRefreshTokenKey(user_id);
    EXPECT_EQ(result, "refresh_token:18446744073709551615");
}

TEST(RedisKeyPrefix, BuildAccessTokenBlacklistKey_ValidJti) {
    std::string jti = "abc123def456";
    auto result = RedisKeyPrefix::BuildAccessTokenBlacklistKey(jti);
    EXPECT_EQ(result, "access_token_blacklist:abc123def456");
}

TEST(RedisKeyPrefix, BuildAccessTokenBlacklistKey_EmptyJti) {
    std::string jti;
    auto result = RedisKeyPrefix::BuildAccessTokenBlacklistKey(jti);
    EXPECT_EQ(result, "access_token_blacklist:");
}

TEST(RedisKeyPrefix, BuildAccessTokenBlacklistKey_LongJti) {
    std::string jti(100, 'a');
    auto result = RedisKeyPrefix::BuildAccessTokenBlacklistKey(jti);
    EXPECT_EQ(result, "access_token_blacklist:" + std::string(100, 'a'));
}

TEST(RedisKeyPrefix, BuildLoginRateLimitKey_ValidIPv4) {
    std::string ip = "192.168.1.1";
    auto result = RedisKeyPrefix::BuildLoginRateLimitKey(ip);
    EXPECT_EQ(result, "rate:login:192.168.1.1");
}

TEST(RedisKeyPrefix, BuildLoginRateLimitKey_ValidIPv6) {
    std::string ip = "2001:db8::1";
    auto result = RedisKeyPrefix::BuildLoginRateLimitKey(ip);
    EXPECT_EQ(result, "rate:login:2001:db8::1");
}

TEST(RedisKeyPrefix, BuildLoginRateLimitKey_WithPort) {
    std::string ip = "192.168.1.1:8080";
    auto result = RedisKeyPrefix::BuildLoginRateLimitKey(ip);
    EXPECT_EQ(result, "rate:login:192.168.1.1");
}

TEST(RedisKeyPrefix, BuildLoginRateLimitKey_IPv6WithPort) {
    std::string ip = "[2001:db8::1]:8080";
    auto result = RedisKeyPrefix::BuildLoginRateLimitKey(ip);
    EXPECT_EQ(result, "rate:login:2001:db8::1");
}

// ==================== Share Token Redis Key Tests ====================

TEST(RedisKeyPrefix, BuildShareTokenKey_ValidShareCodeAndHash) {
    std::string share_code = "AbCd1234";
    std::string token_hash = "a1b2c3d4e5f6";
    auto result = RedisKeyPrefix::BuildShareTokenKey(share_code, token_hash);
    EXPECT_EQ(result, "share_token:AbCd1234:a1b2c3d4e5f6");
}

TEST(RedisKeyPrefix, BuildShareTokenKey_EmptyTokenHash) {
    std::string share_code = "XyZ999";
    std::string token_hash;
    auto result = RedisKeyPrefix::BuildShareTokenKey(share_code, token_hash);
    EXPECT_EQ(result, "share_token:XyZ999:");
}

TEST(RedisKeyPrefix, BuildShareTokenKey_EmptyShareCode) {
    std::string share_code;
    std::string token_hash = "hash123";
    auto result = RedisKeyPrefix::BuildShareTokenKey(share_code, token_hash);
    EXPECT_EQ(result, "share_token::hash123");
}

TEST(RedisKeyPrefix, BuildShareTokenBlacklistKey_ValidHash) {
    std::string token_hash = "revoked_token_hash_abc";
    auto result = RedisKeyPrefix::BuildShareTokenBlacklistKey(token_hash);
    EXPECT_EQ(result, "share_token_blacklist:revoked_token_hash_abc");
}

TEST(RedisKeyPrefix, BuildShareTokenBlacklistKey_EmptyHash) {
    std::string token_hash;
    auto result = RedisKeyPrefix::BuildShareTokenBlacklistKey(token_hash);
    EXPECT_EQ(result, "share_token_blacklist:");
}

TEST(RedisKeyPrefix, BuildSharePasswordRateLimitKey_ValidShareCodeAndIP) {
    std::string share_code = "AbCd12";
    std::string ip = "192.168.1.100";
    auto result = RedisKeyPrefix::BuildSharePasswordRateLimitKey(share_code, ip);
    EXPECT_EQ(result, "rate:share_password:AbCd12:192.168.1.100");
}

TEST(RedisKeyPrefix, BuildSharePasswordRateLimitKey_IPWithPort) {
    std::string share_code = "XyZ99";
    std::string ip = "10.0.0.1:8080";
    auto result = RedisKeyPrefix::BuildSharePasswordRateLimitKey(share_code, ip);
    EXPECT_EQ(result, "rate:share_password:XyZ99:10.0.0.1");
}

TEST(RedisKeyPrefix, BuildSharePasswordRateLimitKey_IPv6WithPort) {
    std::string share_code = "Test01";
    std::string ip = "[2001:db8::1]:9090";
    auto result = RedisKeyPrefix::BuildSharePasswordRateLimitKey(share_code, ip);
    EXPECT_EQ(result, "rate:share_password:Test01:2001:db8::1");
}
