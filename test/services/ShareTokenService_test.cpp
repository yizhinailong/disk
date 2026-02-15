/**
 * @file ShareTokenService_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief ShareTokenService 单元测试
 * @version 0.1
 * @date 2026-02-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "services/ShareTokenService.hpp"

#include <chrono>

#include <gtest/gtest.h>
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/traits.h>

#include "utils/ErrorCode.hpp"

namespace {

    using disk::error::Code;
    using disk::services::ShareTokenService;

    static constexpr const char* TEST_JWT_SECRET = "test_secret_key_for_share_token_32b";

    TEST(ShareTokenServiceTest, GenerateToken_ValidInput_ReturnsToken) {
        std::string jwt_secret = TEST_JWT_SECRET;
        std::string share_code = "AbCd12";
        uint64_t share_id = 12345;

        auto token_result = ShareTokenService::GenerateToken(jwt_secret, share_code, share_id);

        EXPECT_TRUE(token_result.has_value()) << "Should generate token successfully";
        EXPECT_FALSE(token_result.value().empty()) << "Token should not be empty";
    }

    TEST(ShareTokenServiceTest, GenerateToken_EmptyShareCode_ReturnsToken) {
        std::string jwt_secret = TEST_JWT_SECRET;
        std::string share_code;
        uint64_t share_id = 12345;

        auto token_result = ShareTokenService::GenerateToken(jwt_secret, share_code, share_id);

        EXPECT_TRUE(token_result.has_value()) << "Should generate token even with empty share_code";
    }

    TEST(ShareTokenServiceTest, VerifyToken_ValidToken_ReturnsClaims) {
        std::string jwt_secret = TEST_JWT_SECRET;
        std::string share_code = "AbCd12";
        uint64_t share_id = 12345;

        auto token_result = ShareTokenService::GenerateToken(jwt_secret, share_code, share_id);
        ASSERT_TRUE(token_result.has_value()) << "Token generation should succeed";

        auto verify_result = ShareTokenService::VerifyToken(jwt_secret, token_result.value());

        EXPECT_TRUE(verify_result.has_value()) << "Should verify token successfully";
        EXPECT_EQ(verify_result.value().share_code, share_code);
        EXPECT_EQ(verify_result.value().share_id, share_id);
        EXPECT_FALSE(verify_result.value().jti.empty()) << "JTI should be present";
    }

    TEST(ShareTokenServiceTest, VerifyToken_EmptyToken_ReturnsMalformedError) {
        std::string jwt_secret = TEST_JWT_SECRET;
        std::string empty_token;

        auto verify_result = ShareTokenService::VerifyToken(jwt_secret, empty_token);

        EXPECT_FALSE(verify_result.has_value()) << "Should fail with empty token";
        EXPECT_EQ(verify_result.error().code, Code::TokenMalformed);
    }

    TEST(ShareTokenServiceTest, VerifyToken_InvalidBase64Token_ReturnsMalformedError) {
        std::string jwt_secret = TEST_JWT_SECRET;
        std::string invalid_token = "not.a.valid.jwt";

        auto verify_result = ShareTokenService::VerifyToken(jwt_secret, invalid_token);

        EXPECT_FALSE(verify_result.has_value()) << "Should fail verification";
        EXPECT_EQ(verify_result.error().code, Code::TokenMalformed);
    }

    TEST(ShareTokenServiceTest, VerifyToken_WrongSecret_ReturnsMalformedError) {
        std::string jwt_secret = TEST_JWT_SECRET;
        std::string wrong_secret = "wrong_secret_key_for_share_token_";
        std::string share_code = "AbCd12";
        uint64_t share_id = 12345;

        auto token_result = ShareTokenService::GenerateToken(jwt_secret, share_code, share_id);
        ASSERT_TRUE(token_result.has_value());

        auto verify_result = ShareTokenService::VerifyToken(wrong_secret, token_result.value());

        EXPECT_FALSE(verify_result.has_value()) << "Should fail with wrong secret";
        EXPECT_EQ(verify_result.error().code, Code::TokenMalformed);
    }

    TEST(ShareTokenServiceTest, VerifyToken_ExpiredToken_ReturnsTokenExpiredError) {
        using traits = jwt::traits::open_source_parsers_jsoncpp;

        auto now = std::chrono::system_clock::now();
        auto expired_time = now - std::chrono::hours(1);

        jwt::builder<jwt::default_clock, traits> builder{ jwt::default_clock{} };
        auto expired_token = builder
                                 .set_issuer("disk_share")
                                 .set_type("JWT")
                                 .set_subject("12345")
                                 .set_payload_claim("share_code", "AbCd12")
                                 .set_payload_claim("type", "share")
                                 .set_payload_claim("jti", "test-jti-expired")
                                 .set_issued_at(expired_time)
                                 .set_expires_at(expired_time)
                                 .sign(jwt::algorithm::hs256{ TEST_JWT_SECRET });

        auto verify_result = ShareTokenService::VerifyToken(TEST_JWT_SECRET, expired_token);

        EXPECT_FALSE(verify_result.has_value()) << "Should fail with expired token";
        EXPECT_EQ(verify_result.error().code, Code::TokenExpired);
    }

    TEST(ShareTokenServiceTest, VerifyToken_WrongTokenType_ReturnsTokenWrongTypeError) {
        using traits = jwt::traits::open_source_parsers_jsoncpp;

        auto now = std::chrono::system_clock::now();

        jwt::builder<jwt::default_clock, traits> builder{ jwt::default_clock{} };
        auto wrong_type_token = builder
                                    .set_issuer("disk_share")
                                    .set_type("JWT")
                                    .set_subject("12345")
                                    .set_payload_claim("share_code", "AbCd12")
                                    .set_payload_claim("type", "access")
                                    .set_payload_claim("jti", "test-jti-wrong-type")
                                    .set_issued_at(now)
                                    .set_expires_at(now + std::chrono::hours(1))
                                    .sign(jwt::algorithm::hs256{ TEST_JWT_SECRET });

        auto verify_result = ShareTokenService::VerifyToken(TEST_JWT_SECRET, wrong_type_token);

        EXPECT_FALSE(verify_result.has_value()) << "Should fail with wrong token type";
        EXPECT_EQ(verify_result.error().code, Code::TokenWrongType);
    }

    TEST(ShareTokenServiceTest, VerifyToken_WrongIssuer_ReturnsMalformedError) {
        using traits = jwt::traits::open_source_parsers_jsoncpp;

        auto now = std::chrono::system_clock::now();

        jwt::builder<jwt::default_clock, traits> builder{ jwt::default_clock{} };
        auto wrong_issuer_token = builder
                                      .set_issuer("disk")
                                      .set_type("JWT")
                                      .set_subject("12345")
                                      .set_payload_claim("share_code", "AbCd12")
                                      .set_payload_claim("type", "share")
                                      .set_payload_claim("jti", "test-jti-wrong-issuer")
                                      .set_issued_at(now)
                                      .set_expires_at(now + std::chrono::hours(1))
                                      .sign(jwt::algorithm::hs256{ TEST_JWT_SECRET });

        auto verify_result = ShareTokenService::VerifyToken(TEST_JWT_SECRET, wrong_issuer_token);

        EXPECT_FALSE(verify_result.has_value()) << "Should fail with wrong issuer";
        EXPECT_EQ(verify_result.error().code, Code::TokenMalformed);
    }

    TEST(ShareTokenServiceTest, VerifyToken_GarbageString_ReturnsMalformedError) {
        auto verify_result = ShareTokenService::VerifyToken(TEST_JWT_SECRET, "garbage!!!@#$%");

        EXPECT_FALSE(verify_result.has_value()) << "Should fail with garbage string";
        EXPECT_EQ(verify_result.error().code, Code::TokenMalformed);
    }

    TEST(ShareTokenServiceTest, VerifyToken_RandomBase64String_ReturnsMalformedError) {
        auto verify_result = ShareTokenService::VerifyToken(TEST_JWT_SECRET, "YWJjZGVmZ2hpamtsbW5vcA");

        EXPECT_FALSE(verify_result.has_value()) << "Should fail with random base64";
        EXPECT_EQ(verify_result.error().code, Code::TokenMalformed);
    }

    TEST(ShareTokenServiceTest, ExtractTokenHash_ValidToken_ReturnsHash) {
        std::string jwt_secret = TEST_JWT_SECRET;
        std::string share_code = "AbCd12";
        uint64_t share_id = 12345;

        auto token_result = ShareTokenService::GenerateToken(jwt_secret, share_code, share_id);
        ASSERT_TRUE(token_result.has_value());

        auto hash_result = ShareTokenService::ExtractTokenHash(token_result.value());

        EXPECT_TRUE(hash_result.has_value()) << "Should extract hash successfully";
        EXPECT_EQ(hash_result.value().length(), 64) << "Hash should be 64 hex characters";
    }

    TEST(ShareTokenServiceTest, ExtractTokenHash_EmptyToken_ReturnsError) {
        std::string empty_token;

        auto hash_result = ShareTokenService::ExtractTokenHash(empty_token);

        EXPECT_FALSE(hash_result.has_value()) << "Should fail with empty token";
    }

    TEST(ShareTokenServiceTest, ExtractTokenHash_AnyNonEmptyString_ReturnsHash) {
        std::string any_string = "not_a_valid_jwt_token";

        auto hash_result = ShareTokenService::ExtractTokenHash(any_string);

        EXPECT_TRUE(hash_result.has_value()) << "Should hash any non-empty string";
        EXPECT_EQ(hash_result.value().length(), 64) << "Hash should be 64 hex characters";
    }

    TEST(ShareTokenServiceTest, GenerateToken_DifferentShareCodes_DifferentTokens) {
        std::string jwt_secret = TEST_JWT_SECRET;
        uint64_t share_id = 12345;

        auto token1 = ShareTokenService::GenerateToken(jwt_secret, "code1", share_id);
        auto token2 = ShareTokenService::GenerateToken(jwt_secret, "code2", share_id);

        ASSERT_TRUE(token1.has_value());
        ASSERT_TRUE(token2.has_value());
        EXPECT_NE(token1.value(), token2.value()) << "Different share codes should produce different tokens";
    }

    TEST(ShareTokenServiceTest, GenerateToken_SameInputSameShareCode_DifferentJtis) {
        std::string jwt_secret = TEST_JWT_SECRET;
        std::string share_code = "AbCd12";
        uint64_t share_id = 12345;

        auto token1 = ShareTokenService::GenerateToken(jwt_secret, share_code, share_id);
        auto token2 = ShareTokenService::GenerateToken(jwt_secret, share_code, share_id);

        ASSERT_TRUE(token1.has_value());
        ASSERT_TRUE(token2.has_value());

        auto verify1 = ShareTokenService::VerifyToken(jwt_secret, token1.value());
        auto verify2 = ShareTokenService::VerifyToken(jwt_secret, token2.value());

        ASSERT_TRUE(verify1.has_value());
        ASSERT_TRUE(verify2.has_value());

        EXPECT_NE(verify1.value().jti, verify2.value().jti) << "Each token should have unique JTI";
    }

    TEST(ShareTokenServiceTest, GetDefaultExpireSeconds_ReturnsPositiveValue) {
        auto expire_seconds = ShareTokenService::GetShareTokenExpireSeconds();

        EXPECT_GT(expire_seconds, 0) << "Expire seconds should be positive";
        EXPECT_EQ(expire_seconds, 3600) << "Default expiry is 1 hour";
    }

} // namespace
