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

    TEST_F(JwtAuthFilterTest, ClearRevocationCache) {
        auto token_service = TokenService::GetInstance();
        ASSERT_NE(token_service, nullptr);

        token_service->ClearRevocationCache();
        token_service->ClearRevocationCache();

        auto token = BuildAccessToken(7, "cache_clear_user", "jti-clear-cache");
        auto verify_result = token_service->VerifyAccessToken(token);
        ASSERT_TRUE(verify_result.has_value());
        EXPECT_EQ(verify_result.value().jti, "jti-clear-cache");
    }

    TEST_F(JwtAuthFilterTest, DISABLED_RevocationCacheNonRevokedToken) {
        SUCCEED() << "Requires Redis/Drogon runtime to assert Redis bypass on cache hit";
    }

    TEST_F(JwtAuthFilterTest, DISABLED_RevocationCacheRevokedToken) {
        SUCCEED() << "Requires Redis/Drogon runtime to validate revoked-token rejection path";
    }

    TEST_F(JwtAuthFilterTest, DISABLED_RevocationCacheInvalidation) {
        SUCCEED() << "Requires Redis/Drogon runtime to validate immediate in-memory invalidation";
    }

    TEST_F(JwtAuthFilterTest, DISABLED_RevocationCacheExpiry) {
        SUCCEED() << "Requires Redis/Drogon runtime to validate cache TTL expiry behavior";
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

    // ================================================================================
    // Error code contract tests — lock in auth error codes before optimization
    //
    // JwtAuthFilter doFilter maps to these error codes:
    //   empty Authorization   → TokenMissing  (line 32)
    //   non-Bearer prefix     → TokenMalformed (line 40)
    //   VerifyAccessToken fail → forwarded error (TokenExpired/InvalidToken/TokenWrongType/TokenMalformed)
    //   IsAccessTokenRevoked   → TokenRevoked  (line 64)
    //   success               → user_id + username attributes (line 67-68)
    //
    // These tests lock in the numeric values, HTTP status codes, and messages.
    // ================================================================================

    // --- TokenMissing (empty Authorization header) ---

    TEST_F(JwtAuthFilterTest, TokenMissingErrorCodeValue) {
        EXPECT_EQ(static_cast<uint32_t>(Code::TokenMissing), 40106u);
    }

    TEST_F(JwtAuthFilterTest, TokenMissingHttpStatusIs401) {
        EXPECT_EQ(disk::error::GetHttpStatus(Code::TokenMissing), drogon::k401Unauthorized);
    }

    TEST_F(JwtAuthFilterTest, TokenMissingErrorMessage) {
        EXPECT_EQ(disk::error::GetErrorMessage(Code::TokenMissing), std::string("Token not provided"));
    }

    // --- TokenMalformed (non-Bearer prefix / garbled token) ---

    TEST_F(JwtAuthFilterTest, TokenMalformedErrorCodeValue) {
        EXPECT_EQ(static_cast<uint32_t>(Code::TokenMalformed), 40107u);
    }

    TEST_F(JwtAuthFilterTest, TokenMalformedHttpStatusIs401) {
        EXPECT_EQ(disk::error::GetHttpStatus(Code::TokenMalformed), drogon::k401Unauthorized);
    }

    TEST_F(JwtAuthFilterTest, TokenMalformedErrorMessage) {
        EXPECT_EQ(disk::error::GetErrorMessage(Code::TokenMalformed), std::string("Token format error"));
    }

    // --- TokenExpired ---

    TEST_F(JwtAuthFilterTest, TokenExpiredErrorCodeValue) {
        EXPECT_EQ(static_cast<uint32_t>(Code::TokenExpired), 40108u);
    }

    TEST_F(JwtAuthFilterTest, TokenExpiredHttpStatusIs401) {
        EXPECT_EQ(disk::error::GetHttpStatus(Code::TokenExpired), drogon::k401Unauthorized);
    }

    TEST_F(JwtAuthFilterTest, TokenExpiredErrorMessage) {
        EXPECT_EQ(disk::error::GetErrorMessage(Code::TokenExpired), std::string("Token expired"));
    }

    // --- TokenWrongType (refresh token used where access expected) ---

    TEST_F(JwtAuthFilterTest, TokenWrongTypeErrorCodeValue) {
        EXPECT_EQ(static_cast<uint32_t>(Code::TokenWrongType), 40109u);
    }

    TEST_F(JwtAuthFilterTest, TokenWrongTypeHttpStatusIs401) {
        EXPECT_EQ(disk::error::GetHttpStatus(Code::TokenWrongType), drogon::k401Unauthorized);
    }

    TEST_F(JwtAuthFilterTest, TokenWrongTypeErrorMessage) {
        EXPECT_EQ(disk::error::GetErrorMessage(Code::TokenWrongType), std::string("Token type error"));
    }

    // --- TokenRevoked (post-verification revocation check) ---

    TEST_F(JwtAuthFilterTest, TokenRevokedErrorCodeValue) {
        EXPECT_EQ(static_cast<uint32_t>(Code::TokenRevoked), 40111u);
    }

    TEST_F(JwtAuthFilterTest, TokenRevokedHttpStatusIs401) {
        EXPECT_EQ(disk::error::GetHttpStatus(Code::TokenRevoked), drogon::k401Unauthorized);
    }

    TEST_F(JwtAuthFilterTest, TokenRevokedErrorMessage) {
        EXPECT_EQ(disk::error::GetErrorMessage(Code::TokenRevoked), std::string("Token revoked"));
    }

    // --- InvalidToken (signature / issuer mismatch) ---

    TEST_F(JwtAuthFilterTest, InvalidTokenErrorCodeValue) {
        EXPECT_EQ(static_cast<uint32_t>(Code::InvalidToken), 40104u);
    }

    TEST_F(JwtAuthFilterTest, InvalidTokenHttpStatusIs401) {
        EXPECT_EQ(disk::error::GetHttpStatus(Code::InvalidToken), drogon::k401Unauthorized);
    }

    TEST_F(JwtAuthFilterTest, InvalidTokenErrorMessage) {
        EXPECT_EQ(disk::error::GetErrorMessage(Code::InvalidToken), std::string("Token invalid or expired"));
    }

    // ================================================================================
    // VerifyRefreshToken — refresh token validation paths
    // ================================================================================

    TEST_F(JwtAuthFilterTest, VerifyRefreshTokenValidTokenReturnsUserIdAndJti) {
        using traits = jwt::traits::open_source_parsers_jsoncpp;
        auto now = std::chrono::system_clock::now();
        jwt::builder<jwt::default_clock, traits> builder{ jwt::default_clock{} };
        auto refresh_token = builder
                                 .set_issuer("disk")
                                 .set_type("JWT")
                                 .set_subject("42")
                                 .set_payload_claim("type", "refresh")
                                 .set_payload_claim("jti", "jti-refresh-valid-001")
                                 .set_issued_at(now)
                                 .set_expires_at(now + std::chrono::hours(168))
                                 .sign(jwt::algorithm::hs256{ TEST_JWT_SECRET });

        auto result = TokenService::GetInstance()->VerifyRefreshToken(refresh_token);

        ASSERT_TRUE(result.has_value()) << "Valid refresh token should verify";
        EXPECT_EQ(result.value().first, 42u);
        EXPECT_EQ(result.value().second, "jti-refresh-valid-001");
    }

    TEST_F(JwtAuthFilterTest, VerifyRefreshTokenExpiredTokenReturnsTokenExpired) {
        using traits = jwt::traits::open_source_parsers_jsoncpp;
        auto now = std::chrono::system_clock::now();
        auto past = now - std::chrono::hours(1);
        jwt::builder<jwt::default_clock, traits> builder{ jwt::default_clock{} };
        auto expired_refresh = builder
                                   .set_issuer("disk")
                                   .set_type("JWT")
                                   .set_subject("1")
                                   .set_payload_claim("type", "refresh")
                                   .set_payload_claim("jti", "jti-refresh-expired")
                                   .set_issued_at(past)
                                   .set_expires_at(past)
                                   .sign(jwt::algorithm::hs256{ TEST_JWT_SECRET });

        auto result = TokenService::GetInstance()->VerifyRefreshToken(expired_refresh);

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, Code::TokenExpired);
    }

    TEST_F(JwtAuthFilterTest, VerifyRefreshTokenWithAccessTokenReturnsTokenWrongType) {
        auto access_token = BuildAccessToken(1, "user", "jti-access-as-refresh");
        auto result = TokenService::GetInstance()->VerifyRefreshToken(access_token);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, Code::TokenWrongType);
    }

    TEST_F(JwtAuthFilterTest, VerifyRefreshTokenEmptyTokenReturnsTokenMalformed) {
        auto result = TokenService::GetInstance()->VerifyRefreshToken("");
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, Code::TokenMalformed);
    }

    TEST_F(JwtAuthFilterTest, VerifyRefreshTokenWrongSecretReturnsError) {
        using traits = jwt::traits::open_source_parsers_jsoncpp;
        auto now = std::chrono::system_clock::now();
        jwt::builder<jwt::default_clock, traits> builder{ jwt::default_clock{} };
        auto bad_secret_token = builder
                                    .set_issuer("disk")
                                    .set_type("JWT")
                                    .set_subject("1")
                                    .set_payload_claim("type", "refresh")
                                    .set_payload_claim("jti", "jti-bad-secret-refresh")
                                    .set_issued_at(now)
                                    .set_expires_at(now + std::chrono::hours(168))
                                    .sign(jwt::algorithm::hs256{ "wrong_secret_key_for_share_token_" });

        auto result = TokenService::GetInstance()->VerifyRefreshToken(bad_secret_token);
        ASSERT_FALSE(result.has_value());
        EXPECT_TRUE(
            result.error().code == Code::InvalidRefreshToken ||
            result.error().code == Code::TokenMalformed
        ) << "Expected InvalidRefreshToken or TokenMalformed for wrong secret, got: "
          << static_cast<uint32_t>(result.error().code);
    }

    // ================================================================================
    // GenerateTokens — token pair generation contract
    // ================================================================================

    TEST_F(JwtAuthFilterTest, GenerateTokensReturnsTwoDistinctNonEmptyTokens) {
        auto [access_token, refresh_token] = TokenService::GetInstance()->GenerateTokens(42, "gen_user");
        EXPECT_FALSE(access_token.empty());
        EXPECT_FALSE(refresh_token.empty());
        EXPECT_NE(access_token, refresh_token);
    }

    TEST_F(JwtAuthFilterTest, GenerateTokensAccessVerifiableWithCorrectClaims) {
        auto [access_token, refresh_token] = TokenService::GetInstance()->GenerateTokens(42, "gen_user");

        auto result = TokenService::GetInstance()->VerifyAccessToken(access_token);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value().user_id, 42u);
        EXPECT_EQ(result.value().username, "gen_user");
        EXPECT_FALSE(result.value().jti.empty());
    }

    TEST_F(JwtAuthFilterTest, GenerateTokensRefreshVerifiableWithCorrectUserId) {
        auto [access_token, refresh_token] = TokenService::GetInstance()->GenerateTokens(42, "gen_user");

        auto result = TokenService::GetInstance()->VerifyRefreshToken(refresh_token);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value().first, 42u);
        EXPECT_FALSE(result.value().second.empty()) << "Refresh token should have a JTI";
    }

    TEST_F(JwtAuthFilterTest, GenerateTokensAccessAndRefreshHaveDifferentJtis) {
        auto [access_token, refresh_token] = TokenService::GetInstance()->GenerateTokens(42, "gen_user");

        auto access_result = TokenService::GetInstance()->VerifyAccessToken(access_token);
        auto refresh_result = TokenService::GetInstance()->VerifyRefreshToken(refresh_token);
        ASSERT_TRUE(access_result.has_value());
        ASSERT_TRUE(refresh_result.has_value());

        EXPECT_NE(access_result.value().jti, refresh_result.value().second)
            << "Access and refresh tokens should have different JTIs";
    }

    // ================================================================================
    // VerifyAccessToken — additional edge cases
    // ================================================================================

    TEST_F(JwtAuthFilterTest, VerifyAccessTokenTokenWithoutUsernameClaimReturnsMalformed) {
        using traits = jwt::traits::open_source_parsers_jsoncpp;
        auto now = std::chrono::system_clock::now();
        jwt::builder<jwt::default_clock, traits> builder{ jwt::default_clock{} };
        // Token without username claim — VerifyAccessToken calls
        // decoded.get_payload_claim("username").as_string() which will throw
        auto token = builder
                         .set_issuer("disk")
                         .set_type("JWT")
                         .set_subject("1")
                         .set_payload_claim("type", "access")
                         .set_payload_claim("jti", "jti-no-username")
                         .set_issued_at(now)
                         .set_expires_at(now + std::chrono::hours(2))
                         .sign(jwt::algorithm::hs256{ TEST_JWT_SECRET });

        auto result = TokenService::GetInstance()->VerifyAccessToken(token);
        // Missing "username" claim → std::exception → TokenMalformed
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, Code::TokenMalformed);
    }

    TEST_F(JwtAuthFilterTest, VerifyAccessTokenTokenWithoutJtiClaimReturnsMalformed) {
        using traits = jwt::traits::open_source_parsers_jsoncpp;
        auto now = std::chrono::system_clock::now();
        jwt::builder<jwt::default_clock, traits> builder{ jwt::default_clock{} };
        auto token = builder
                         .set_issuer("disk")
                         .set_type("JWT")
                         .set_subject("1")
                         .set_payload_claim("username", "user")
                         .set_payload_claim("type", "access")
                         // No jti claim
                         .set_issued_at(now)
                         .set_expires_at(now + std::chrono::hours(2))
                         .sign(jwt::algorithm::hs256{ TEST_JWT_SECRET });

        auto result = TokenService::GetInstance()->VerifyAccessToken(token);
        // Missing "jti" claim → std::exception → TokenMalformed
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, Code::TokenMalformed);
    }

} // namespace
