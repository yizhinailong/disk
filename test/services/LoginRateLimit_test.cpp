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

    /// ================================================================================
    /// Auth error code contract tests — login failure paths
    /// ================================================================================

    TEST(LoginRateLimit, InvalidCredentialsErrorCodeValue) {
        EXPECT_EQ(static_cast<uint32_t>(Code::InvalidCredentials), 40101u);
    }

    TEST(LoginRateLimit, InvalidCredentialsHttpStatusIs401) {
        EXPECT_EQ(disk::error::GetHttpStatus(Code::InvalidCredentials), drogon::k401Unauthorized);
    }

    TEST(LoginRateLimit, InvalidCredentialsErrorMessage) {
        EXPECT_EQ(
            disk::error::GetErrorMessage(Code::InvalidCredentials),
            std::string("Invalid username or password")
        );
    }

    TEST(LoginRateLimit, AccountLockedErrorCodeValue) {
        EXPECT_EQ(static_cast<uint32_t>(Code::AccountLocked), 40102u);
    }

    TEST(LoginRateLimit, AccountLockedHttpStatusIs401) {
        EXPECT_EQ(disk::error::GetHttpStatus(Code::AccountLocked), drogon::k401Unauthorized);
    }

    TEST(LoginRateLimit, AccountLockedErrorMessage) {
        EXPECT_EQ(
            disk::error::GetErrorMessage(Code::AccountLocked),
            std::string("Account locked, please try again later")
        );
    }

    TEST(LoginRateLimit, AccountDisabledErrorCodeValue) {
        EXPECT_EQ(static_cast<uint32_t>(Code::AccountDisabled), 40103u);
    }

    TEST(LoginRateLimit, AccountDisabledHttpStatusIs401) {
        EXPECT_EQ(disk::error::GetHttpStatus(Code::AccountDisabled), drogon::k401Unauthorized);
    }

    TEST(LoginRateLimit, InvalidRefreshTokenErrorCodeValue) {
        EXPECT_EQ(static_cast<uint32_t>(Code::InvalidRefreshToken), 40105u);
    }

    TEST(LoginRateLimit, RefreshTokenAlreadyUsedErrorCodeValue) {
        EXPECT_EQ(static_cast<uint32_t>(Code::RefreshTokenAlreadyUsed), 40110u);
    }

    TEST(LoginRateLimit, AllAuthErrorCodesMapTo401) {
        const std::vector<Code> auth_codes = {
            Code::InvalidCredentials,
            Code::AccountLocked,
            Code::AccountDisabled,
            Code::InvalidToken,
            Code::TokenMissing,
            Code::TokenMalformed,
            Code::TokenExpired,
            Code::TokenWrongType,
            Code::InvalidRefreshToken,
            Code::RefreshTokenAlreadyUsed,
            Code::TokenRevoked,
        };
        for (const auto& code : auth_codes) {
            EXPECT_EQ(disk::error::GetHttpStatus(code), drogon::k401Unauthorized)
                << "Auth error code " << static_cast<uint32_t>(code) << " should map to 401";
        }
    }

} ///< namespace
