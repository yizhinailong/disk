/**
 * @file LoginRateLimit_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 登录频率限制契约测试
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <string>

#include <gtest/gtest.h>

#include "utils/ErrorCode.hpp"
#include "utils/RedisKeyPrefix.hpp"

namespace {

    using disk::error::Code;
    using disk::redis::RedisKeyPrefix;

    TEST(LoginRateLimit, TooManyRequestsMapsToHttp429) {
        EXPECT_EQ(disk::error::GetHttpStatus(Code::TooManyRequests), drogon::k429TooManyRequests);
    }

    TEST(LoginRateLimit, TooManyRequestsDefaultMessageIsStable) {
        EXPECT_EQ(disk::error::GetErrorMessage(Code::TooManyRequests), std::string("Too many requests"));
    }

    TEST(LoginRateLimit, BuildLoginRateLimitKeyNormalizesIpv4SourcePort) {
        EXPECT_EQ(
            RedisKeyPrefix::BuildLoginRateLimitKey("127.0.0.11:54321"),
            std::string("rate:login:127.0.0.11")
        );
    }

    TEST(LoginRateLimit, BuildLoginRateLimitKeyKeepsDifferentLoopbackIpsIndependent) {
        EXPECT_NE(
            RedisKeyPrefix::BuildLoginRateLimitKey("127.0.0.11:54321"),
            RedisKeyPrefix::BuildLoginRateLimitKey("127.0.0.12:54321")
        );
    }

} // namespace
