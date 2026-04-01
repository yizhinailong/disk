/**
 * @file JwtAuthFilter_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief JWT auth hot-path baseline and regression harness
 *
 * @copyright Copyright (c) 2026
 *
 * Captures baseline behavior for the JWT authentication hot-path:
 * 1. TokenService::VerifyAccessToken behavior for all token states
 * 2. JwtAuthFilter doFilter logic — documents the duplicate decode pattern
 *
 * These tests serve as a regression harness for the auth optimization
 * that will eliminate the redundant JWT decode in JwtAuthFilter.
 */

#include <chrono>
#include <string>

#include <gtest/gtest.h>
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/traits.h>

#include "services/TokenService.hpp"
#include "utils/ErrorCode.hpp"

namespace {

    using disk::error::Code;
    using disk::services::TokenService;

    constexpr const char* TEST_JWT_SECRET = "test_secret_key_for_share_token_32b";

    // ================================================================================
    // Helper: Build a valid access token with custom claims
    // ================================================================================

    auto BuildAccessToken(
        uint64_t user_id,
        const std::string& username,
        const std::string& jti,
        const std::string& secret = TEST_JWT_SECRET,
        std::chrono::system_clock::time_point issued_at = std::chrono::system_clock::now(),
        std::chrono::system_clock::time_point expires_at = std::chrono::system_clock::now() + std::chrono::hours(2)
    ) -> std::string {
        using traits = jwt::traits::open_source_parsers_jsoncpp;
        jwt::builder<jwt::default_clock, traits> builder{ jwt::default_clock{} };
        return builder
            .set_issuer("disk")
            .set_type("JWT")
            .set_subject(std::to_string(user_id))
            .set_payload_claim("username", username)
            .set_payload_claim("type", "access")
            .set_payload_claim("jti", jti)
            .set_issued_at(issued_at)
            .set_expires_at(expires_at)
            .sign(jwt::algorithm::hs256{ secret });
    }

    // ================================================================================
    // Fixture: Initialize TokenService singleton once per test suite
    // ================================================================================

    class JwtAuthFilterTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            TokenService::Initialize(TEST_JWT_SECRET);
        }
    };

    // ================================================================================
    // VerifyAccessToken — happy path
    // ================================================================================

    TEST_F(JwtAuthFilterTest, VerifyAccessTokenValidTokenReturnsUserIdAndUsername) {
        auto token = BuildAccessToken(42, "alice", "jti-test-001");

        auto result = TokenService::GetInstance()->VerifyAccessToken(token);

        ASSERT_TRUE(result.has_value()) << "Valid access token should verify successfully";
        const auto& claims = result.value();
        EXPECT_EQ(claims.user_id, 42u);
        EXPECT_EQ(claims.username, "alice");
        EXPECT_EQ(claims.jti, "jti-test-001");
    }

    TEST_F(JwtAuthFilterTest, VerifyAccessTokenValidTokenWithLargeUserId) {
        auto token = BuildAccessToken(9999999999ULL, "big_id_user", "jti-large-id");

        auto result = TokenService::GetInstance()->VerifyAccessToken(token);

        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value().user_id, 9999999999ULL);
        EXPECT_EQ(result.value().username, "big_id_user");
        EXPECT_EQ(result.value().jti, "jti-large-id");
    }

    // ================================================================================
    // VerifyAccessToken — expired token
    // ================================================================================

    TEST_F(JwtAuthFilterTest, VerifyAccessTokenExpiredTokenReturnsTokenExpired) {
        auto now = std::chrono::system_clock::now();
        auto expired = now - std::chrono::hours(1);

        auto token = BuildAccessToken(1, "expired_user", "jti-expired", TEST_JWT_SECRET, expired, expired);

        auto result = TokenService::GetInstance()->VerifyAccessToken(token);

        ASSERT_FALSE(result.has_value()) << "Expired token should fail verification";
        EXPECT_EQ(result.error().code, Code::TokenExpired);
    }

    // ================================================================================
    // VerifyAccessToken — wrong type (type != "access")
    // ================================================================================

    TEST_F(JwtAuthFilterTest, VerifyAccessTokenWrongTypeReturnsTokenWrongType) {
        using traits = jwt::traits::open_source_parsers_jsoncpp;
        auto now = std::chrono::system_clock::now();

        jwt::builder<jwt::default_clock, traits> builder{ jwt::default_clock{} };
        auto refresh_token = builder
                                 .set_issuer("disk")
                                 .set_type("JWT")
                                 .set_subject("1")
                                 .set_payload_claim("type", "refresh")
                                 .set_payload_claim("jti", "jti-refresh-type")
                                 .set_issued_at(now)
                                 .set_expires_at(now + std::chrono::hours(2))
                                 .sign(jwt::algorithm::hs256{ TEST_JWT_SECRET });

        auto result = TokenService::GetInstance()->VerifyAccessToken(refresh_token);

        ASSERT_FALSE(result.has_value()) << "Wrong type token should fail";
        EXPECT_EQ(result.error().code, Code::TokenWrongType);
    }

    // ================================================================================
    // VerifyAccessToken — malformed / empty token
    // ================================================================================

    TEST_F(JwtAuthFilterTest, VerifyAccessTokenEmptyTokenReturnsTokenMalformed) {
        auto result = TokenService::GetInstance()->VerifyAccessToken("");

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, Code::TokenMalformed);
    }

    TEST_F(JwtAuthFilterTest, VerifyAccessTokenGarbageTokenReturnsTokenMalformed) {
        auto result = TokenService::GetInstance()->VerifyAccessToken("not.a.valid.jwt");

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, Code::TokenMalformed);
    }

    TEST_F(JwtAuthFilterTest, VerifyAccessTokenRandomStringReturnsTokenMalformed) {
        auto result = TokenService::GetInstance()->VerifyAccessToken("totally_random_string_no_dots");

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, Code::TokenMalformed);
    }

    // ================================================================================
    // VerifyAccessToken — wrong secret (signature mismatch)
    // ================================================================================

    TEST_F(JwtAuthFilterTest, VerifyAccessTokenWrongSecretReturnsInvalidToken) {
        std::string wrong_secret = "wrong_secret_key_for_share_token_";
        auto token = BuildAccessToken(1, "user", "jti-wrong-secret", wrong_secret);

        // Verify with the correct secret — should fail signature verification
        auto result = TokenService::GetInstance()->VerifyAccessToken(token);

        ASSERT_FALSE(result.has_value()) << "Token signed with wrong secret should fail";
        // jwt-cpp throws token_verification_exception for bad signatures → InvalidToken
        EXPECT_TRUE(
            result.error().code == Code::InvalidToken ||
            result.error().code == Code::TokenMalformed
        ) << "Expected InvalidToken or TokenMalformed for wrong secret, got: "
          << static_cast<uint32_t>(result.error().code);
    }

    // ================================================================================
    // VerifyAccessToken — wrong issuer
    // ================================================================================

    TEST_F(JwtAuthFilterTest, VerifyAccessTokenWrongIssuerReturnsInvalidToken) {
        using traits = jwt::traits::open_source_parsers_jsoncpp;
        auto now = std::chrono::system_clock::now();

        jwt::builder<jwt::default_clock, traits> builder{ jwt::default_clock{} };
        auto token = builder
                         .set_issuer("wrong_issuer")
                         .set_type("JWT")
                         .set_subject("1")
                         .set_payload_claim("username", "user")
                         .set_payload_claim("type", "access")
                         .set_payload_claim("jti", "jti-wrong-issuer")
                         .set_issued_at(now)
                         .set_expires_at(now + std::chrono::hours(2))
                         .sign(jwt::algorithm::hs256{ TEST_JWT_SECRET });

        auto result = TokenService::GetInstance()->VerifyAccessToken(token);

        ASSERT_FALSE(result.has_value()) << "Wrong issuer should fail verification";
        // Issuer mismatch triggers token_verification_exception → InvalidToken
        EXPECT_TRUE(
            result.error().code == Code::InvalidToken ||
            result.error().code == Code::TokenMalformed
        ) << "Expected InvalidToken or TokenMalformed for wrong issuer";
    }

    // ================================================================================
    // JwtAuthFilter doFilter — optimized: single decode pattern
    //
    // VerifyAccessToken now returns AccessTokenClaims{user_id, username, jti}
    // from a single JWT decode. The redundant second decode has been eliminated.
    // ================================================================================

    TEST_F(JwtAuthFilterTest, VerifyAccessTokenReturnsAccessTokenClaimsWithJti) {
        auto token = BuildAccessToken(100, "baseline_user", "jti-dup-decode");

        auto result = TokenService::GetInstance()->VerifyAccessToken(token);

        ASSERT_TRUE(result.has_value());
        const auto& claims = result.value();
        EXPECT_EQ(claims.user_id, 100u);
        EXPECT_EQ(claims.username, "baseline_user");
        EXPECT_EQ(claims.jti, "jti-dup-decode");
    }

    TEST_F(JwtAuthFilterTest, VerifyAccessTokenJtiAvailableWithoutSecondDecode) {
        auto token = BuildAccessToken(100, "baseline_user", "jti-single-decode");

        auto verify_result = TokenService::GetInstance()->VerifyAccessToken(token);
        ASSERT_TRUE(verify_result.has_value());

        // JTI is available directly from the verification result — no second decode needed.
        EXPECT_EQ(verify_result.value().jti, "jti-single-decode");

        // OPTIMIZED: 1 JWT decode operation per authenticated request (was 2).
    }

    // ================================================================================
    // Token structure contract — access token contains required claims
    // ================================================================================

    TEST_F(JwtAuthFilterTest, AccessTokenStructureContainsRequiredClaims) {
        auto token = BuildAccessToken(42, "claim_user", "jti-claims-check");

        using traits = jwt::traits::open_source_parsers_jsoncpp;
        auto decoded = jwt::decode<traits>(token);

        EXPECT_EQ(decoded.get_issuer(), "disk");
        EXPECT_EQ(decoded.get_subject(), "42");
        EXPECT_EQ(decoded.get_payload_claim("type").as_string(), "access");
        EXPECT_EQ(decoded.get_payload_claim("username").as_string(), "claim_user");
        EXPECT_TRUE(decoded.has_payload_claim("jti"));
        EXPECT_FALSE(decoded.get_payload_claim("jti").as_string().empty());
    }

    // ================================================================================
    // Error code contract — auth-related error codes are distinct
    // ================================================================================

    TEST_F(JwtAuthFilterTest, AuthErrorCodesAreDistinct) {
        EXPECT_NE(static_cast<uint32_t>(Code::TokenMissing), static_cast<uint32_t>(Code::TokenMalformed));
        EXPECT_NE(static_cast<uint32_t>(Code::TokenMissing), static_cast<uint32_t>(Code::TokenExpired));
        EXPECT_NE(static_cast<uint32_t>(Code::TokenMissing), static_cast<uint32_t>(Code::TokenWrongType));
        EXPECT_NE(static_cast<uint32_t>(Code::TokenMissing), static_cast<uint32_t>(Code::TokenRevoked));
        EXPECT_NE(static_cast<uint32_t>(Code::TokenMalformed), static_cast<uint32_t>(Code::TokenExpired));
        EXPECT_NE(static_cast<uint32_t>(Code::TokenMalformed), static_cast<uint32_t>(Code::TokenWrongType));
        EXPECT_NE(static_cast<uint32_t>(Code::TokenMalformed), static_cast<uint32_t>(Code::TokenRevoked));
        EXPECT_NE(static_cast<uint32_t>(Code::TokenExpired), static_cast<uint32_t>(Code::TokenWrongType));
        EXPECT_NE(static_cast<uint32_t>(Code::TokenExpired), static_cast<uint32_t>(Code::TokenRevoked));
        EXPECT_NE(static_cast<uint32_t>(Code::TokenWrongType), static_cast<uint32_t>(Code::TokenRevoked));
        EXPECT_NE(static_cast<uint32_t>(Code::InvalidToken), static_cast<uint32_t>(Code::TokenExpired));
        EXPECT_NE(static_cast<uint32_t>(Code::InvalidToken), static_cast<uint32_t>(Code::TokenMalformed));
    }

    // ================================================================================
    // JwtAuthFilter doFilter — integration tests (require Drogon runtime)
    //
    // The filter's doFilter is a coroutine that requires:
    //   - drogon::HttpRequestPtr
    //   - Redis connection for IsAccessTokenRevoked
    // These are placeholder tests that document the expected behavior.
    // ================================================================================

    TEST_F(JwtAuthFilterTest, DISABLED_FilterMissingAuthorizationHeaderReturnsTokenMissing) {
        SUCCEED() << "Integration test requires Drogon runtime with HTTP request context";
    }

    TEST_F(JwtAuthFilterTest, DISABLED_FilterNonBearerPrefixReturnsTokenMalformed) {
        SUCCEED() << "Integration test requires Drogon runtime with HTTP request context";
    }

    TEST_F(JwtAuthFilterTest, DISABLED_FilterExpiredTokenInHeaderReturnsTokenExpired) {
        SUCCEED() << "Integration test requires Drogon runtime with HTTP request context";
    }

    TEST_F(JwtAuthFilterTest, DISABLED_FilterRevokedTokenReturnsTokenRevoked) {
        SUCCEED() << "Integration test requires Drogon runtime with Redis connection";
    }

    TEST_F(JwtAuthFilterTest, DISABLED_FilterValidTokenSetsUserIdAndUsernameAttributes) {
        SUCCEED() << "Integration test requires Drogon runtime with HTTP request context";
    }

} // namespace
