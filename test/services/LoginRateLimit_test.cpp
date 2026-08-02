/**
 * @file LoginRateLimit_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 登录频率限制契约测试
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "utils/ErrorCode.hpp"
#include "utils/RedisKeyPrefix.hpp"

namespace {

    using disk::error::Code;
    using disk::redis::RedisKeyPrefix;

    auto RepositoryRoot() -> std::filesystem::path {
        return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    }

    auto ReadSourceFile(const std::filesystem::path& relative_path) -> std::string {
        std::ifstream input(RepositoryRoot() / relative_path);
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }

    auto Contains(const std::string& source, std::string_view expected) -> bool {
        return source.find(expected) != std::string::npos;
    }

    auto SourceSection(
        const std::string& source,
        std::string_view begin_marker,
        std::string_view end_marker
    ) -> std::string {
        const auto begin = source.find(begin_marker);
        const auto end = source.find(end_marker, begin);
        if (begin == std::string::npos || end == std::string::npos || end <= begin) {
            return {};
        }
        return source.substr(begin, end - begin);
    }

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

    TEST(LoginRateLimit, AccountLockingUsesPostgresAtomicState) {
        const auto header = ReadSourceFile("src/services/AuthService.hpp");
        const auto source = ReadSourceFile("src/services/AuthService.cpp");
        const auto admin_source = ReadSourceFile("src/services/AdminService.cpp");
        const auto login = SourceSection(
            source,
            "auto AuthService::Login(",
            "auto AuthService::RefreshTokens("
        );
        const auto validate = SourceSection(
            source,
            "auto AuthService::ValidateAccountAccess(",
            "auto AuthService::UpdateLoginInfo("
        );
        const auto successful_login = SourceSection(
            source,
            "auto AuthService::UpdateLoginInfo(",
            "auto AuthService::IncrementLoginAttempts("
        );
        const auto failed_login = SourceSection(
            source,
            "auto AuthService::IncrementLoginAttempts(",
            "} // namespace disk::auth"
        );

        ASSERT_FALSE(login.empty());
        ASSERT_FALSE(validate.empty());
        ASSERT_FALSE(successful_login.empty());
        ASSERT_FALSE(failed_login.empty());

        EXPECT_TRUE(Contains(header, "auto ValidateAccountAccess("));
        EXPECT_TRUE(Contains(header, "-> drogon::Task<Result<void>>;"));
        EXPECT_TRUE(Contains(validate, "locked_until > NOW()"));
        EXPECT_TRUE(Contains(validate, "status = 2 AND locked_until IS NULL"));
        EXPECT_FALSE(Contains(validate, "trantor::Date::now()"));

        EXPECT_TRUE(Contains(successful_login, "UPDATE users SET"));
        EXPECT_TRUE(Contains(successful_login, "last_login_at = NOW()"));
        EXPECT_TRUE(Contains(successful_login, "login_attempts = 0"));
        EXPECT_TRUE(Contains(successful_login, "locked_until = NULL"));
        EXPECT_TRUE(Contains(successful_login, "locked_until <= NOW()"));
        EXPECT_TRUE(Contains(successful_login, "RETURNING id"));
        EXPECT_FALSE(Contains(successful_login, "CoroMapper<Users>"));
        EXPECT_FALSE(Contains(successful_login, "trantor::Date::now()"));

        EXPECT_TRUE(Contains(failed_login, "UPDATE users SET"));
        EXPECT_TRUE(Contains(failed_login, "login_attempts = CASE"));
        EXPECT_TRUE(Contains(failed_login, "NOW() + INTERVAL '15 minutes'"));
        EXPECT_TRUE(Contains(failed_login, "locked_until IS NULL OR locked_until <= NOW()"));
        EXPECT_TRUE(Contains(failed_login, "RETURNING login_attempts"));
        EXPECT_FALSE(Contains(failed_login, "mapper.findOne("));
        EXPECT_FALSE(Contains(failed_login, "user.setStatus(2)"));
        EXPECT_FALSE(Contains(failed_login, "trantor::Date::now()"));

        const auto update_position = login.find("co_await UpdateLoginInfo(");
        const auto token_position = login.find("GenerateTokens(");
        ASSERT_NE(update_position, std::string::npos);
        ASSERT_NE(token_position, std::string::npos);
        EXPECT_LT(update_position, token_position);

        EXPECT_TRUE(Contains(admin_source, "user.setLoginAttempts(0)"));
        EXPECT_TRUE(Contains(admin_source, "user.setLockedUntilToNull()"));
    }

} // namespace
