/**
 * @file TokenService_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 统一 TokenService 的分享令牌契约测试 (RED 阶段)
 *
 * @copyright Copyright (c) 2026
 *
 * 本测试文件是 TDD RED 阶段的一部分。
 * 这些测试定义了统一 TokenService（disk::services 命名空间）
 * 应具备的分享令牌能力契约。
 *
 * 当前状态：测试应失败，因为统一 TokenService 尚未实现分享令牌能力。
 * 完成任务 2 后，这些测试应转为绿色。
 */

#include "services/TokenService.hpp"

#include <chrono>
#include <string>

#include <gtest/gtest.h>
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/traits.h>

#include "utils/ErrorCode.hpp"

namespace {

    using disk::error::Code;
    using disk::services::TokenService;

    constexpr const char* TEST_JWT_SECRET = "test_secret_key_for_share_token_32b";

    // ================================================================================
    // 统一 TokenService 分享令牌静态 API 测试
    // ================================================================================

    /**
     * @brief 测试统一 TokenService 是否提供 GenerateShareToken 静态方法
     *
     * 契约要求：
     * - 静态方法，接受 jwt_secret, share_code, share_id
     * - 返回 Result<std::string>
     */
    TEST(TokenServiceShareTest, GenerateShareTokenValidInputReturnsToken) {
        std::string jwt_secret = TEST_JWT_SECRET;
        std::string share_code = "AbCd12";
        uint64_t share_id = 12345;

        auto token_result = TokenService::GenerateShareToken(jwt_secret, share_code, share_id);

        EXPECT_TRUE(token_result.has_value()) << "Should generate share token successfully";
        EXPECT_FALSE(token_result.value().empty()) << "Token should not be empty";
    }

    /**
     * @brief 测试统一 TokenService 生成的令牌包含正确的 claims
     *
     * 契约要求：
     * - issuer = "disk_share"
     * - type = "share"
     * - claims: share_code, share_id (subject), jti
     * - TTL = 3600 秒
     */
    TEST(TokenServiceShareTest, GenerateShareTokenValidInputCorrectClaims) {
        using traits = jwt::traits::open_source_parsers_jsoncpp;

        std::string jwt_secret = TEST_JWT_SECRET;
        std::string share_code = "AbCd12";
        uint64_t share_id = 12345;

        auto token_result = TokenService::GenerateShareToken(jwt_secret, share_code, share_id);
        ASSERT_TRUE(token_result.has_value()) << "Token generation should succeed";

        // 解码并验证 claims
        auto decoded = jwt::decode<traits>(token_result.value());

        EXPECT_EQ(decoded.get_issuer(), "disk_share") << "Issuer should be 'disk_share'";
        EXPECT_EQ(decoded.get_payload_claim("type").as_string(), "share") << "Type should be 'share'";
        EXPECT_EQ(decoded.get_payload_claim("share_code").as_string(), share_code) << "share_code claim mismatch";
        EXPECT_EQ(decoded.get_subject(), std::to_string(share_id)) << "share_id (subject) mismatch";
        EXPECT_FALSE(decoded.get_payload_claim("jti").as_string().empty()) << "jti should be present";
    }

    /**
     * @brief 测试统一 TokenService 是否提供 VerifyShareToken 静态方法
     *
     * 契约要求：
     * - 静态方法，接受 jwt_secret, token
     * - 返回 Result<ShareTokenClaims>
     */
    TEST(TokenServiceShareTest, VerifyShareTokenValidTokenReturnsClaims) {
        std::string jwt_secret = TEST_JWT_SECRET;
        std::string share_code = "AbCd12";
        uint64_t share_id = 12345;

        auto token_result = TokenService::GenerateShareToken(jwt_secret, share_code, share_id);
        ASSERT_TRUE(token_result.has_value()) << "Token generation should succeed";

        auto verify_result = TokenService::VerifyShareToken(jwt_secret, token_result.value());

        EXPECT_TRUE(verify_result.has_value()) << "Should verify share token successfully";
        EXPECT_EQ(verify_result.value().share_code, share_code);
        EXPECT_EQ(verify_result.value().share_id, share_id);
        EXPECT_FALSE(verify_result.value().jti.empty()) << "JTI should be present";
    }

    /**
     * @brief 测试空令牌验证返回 TokenMalformed 错误
     */
    TEST(TokenServiceShareTest, VerifyShareTokenEmptyTokenReturnsMalformedError) {
        std::string jwt_secret = TEST_JWT_SECRET;
        std::string empty_token;

        auto verify_result = TokenService::VerifyShareToken(jwt_secret, empty_token);

        EXPECT_FALSE(verify_result.has_value()) << "Should fail with empty token";
        EXPECT_EQ(verify_result.error().code, Code::TokenMalformed);
    }

    /**
     * @brief 测试非法格式的令牌返回 TokenMalformed 错误
     */
    TEST(TokenServiceShareTest, VerifyShareTokenInvalidBase64TokenReturnsMalformedError) {
        std::string jwt_secret = TEST_JWT_SECRET;
        std::string invalid_token = "not.a.valid.jwt";

        auto verify_result = TokenService::VerifyShareToken(jwt_secret, invalid_token);

        EXPECT_FALSE(verify_result.has_value()) << "Should fail verification";
        EXPECT_EQ(verify_result.error().code, Code::TokenMalformed);
    }

    /**
     * @brief 测试错误密钥验证返回 TokenMalformed 错误
     */
    TEST(TokenServiceShareTest, VerifyShareTokenWrongSecretReturnsMalformedError) {
        std::string jwt_secret = TEST_JWT_SECRET;
        std::string wrong_secret = "wrong_secret_key_for_share_token_";
        std::string share_code = "AbCd12";
        uint64_t share_id = 12345;

        auto token_result = TokenService::GenerateShareToken(jwt_secret, share_code, share_id);
        ASSERT_TRUE(token_result.has_value());

        auto verify_result = TokenService::VerifyShareToken(wrong_secret, token_result.value());

        EXPECT_FALSE(verify_result.has_value()) << "Should fail with wrong secret";
        EXPECT_EQ(verify_result.error().code, Code::TokenMalformed);
    }

    /**
     * @brief 测试过期令牌返回 TokenExpired 错误
     */
    TEST(TokenServiceShareTest, VerifyShareTokenExpiredTokenReturnsTokenExpiredError) {
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

        auto verify_result = TokenService::VerifyShareToken(TEST_JWT_SECRET, expired_token);

        EXPECT_FALSE(verify_result.has_value()) << "Should fail with expired token";
        EXPECT_EQ(verify_result.error().code, Code::TokenExpired);
    }

    /**
     * @brief 测试错误令牌类型返回 TokenWrongType 错误
     */
    TEST(TokenServiceShareTest, VerifyShareTokenWrongTokenTypeReturnsTokenWrongTypeError) {
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

        auto verify_result = TokenService::VerifyShareToken(TEST_JWT_SECRET, wrong_type_token);

        EXPECT_FALSE(verify_result.has_value()) << "Should fail with wrong token type";
        EXPECT_EQ(verify_result.error().code, Code::TokenWrongType);
    }

    /**
     * @brief 测试错误签发者返回 TokenMalformed 错误
     */
    TEST(TokenServiceShareTest, VerifyShareTokenWrongIssuerReturnsMalformedError) {
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

        auto verify_result = TokenService::VerifyShareToken(TEST_JWT_SECRET, wrong_issuer_token);

        EXPECT_FALSE(verify_result.has_value()) << "Should fail with wrong issuer";
        EXPECT_EQ(verify_result.error().code, Code::TokenMalformed);
    }

    /**
     * @brief 测试统一 TokenService 是否提供 ExtractShareTokenHash 静态方法
     */
    TEST(TokenServiceShareTest, ExtractShareTokenHashValidTokenReturnsHash) {
        std::string jwt_secret = TEST_JWT_SECRET;
        std::string share_code = "AbCd12";
        uint64_t share_id = 12345;

        auto token_result = TokenService::GenerateShareToken(jwt_secret, share_code, share_id);
        ASSERT_TRUE(token_result.has_value());

        auto hash_result = TokenService::ExtractShareTokenHash(token_result.value());

        EXPECT_TRUE(hash_result.has_value()) << "Should extract hash successfully";
        EXPECT_EQ(hash_result.value().length(), 64) << "Hash should be 64 hex characters";
    }

    /**
     * @brief 测试提取空令牌哈希返回错误
     */
    TEST(TokenServiceShareTest, ExtractShareTokenHashEmptyTokenReturnsError) {
        std::string empty_token;

        auto hash_result = TokenService::ExtractShareTokenHash(empty_token);

        EXPECT_FALSE(hash_result.has_value()) << "Should fail with empty token";
    }

    /**
     * @brief 测试统一 TokenService 是否提供 GetShareTokenExpireSeconds 静态方法
     *
     * 契约要求：TTL = 3600 秒（1小时）
     */
    TEST(TokenServiceShareTest, GetShareTokenExpireSecondsReturns3600) {
        auto expire_seconds = TokenService::GetShareTokenExpireSeconds();

        EXPECT_GT(expire_seconds, 0) << "Expire seconds should be positive";
        EXPECT_EQ(expire_seconds, 3600) << "Default expiry is 1 hour (3600 seconds)";
    }

    /**
     * @brief 测试相同输入生成的令牌有不同的 JTI
     */
    TEST(TokenServiceShareTest, GenerateShareTokenSameInputDifferentJtis) {
        std::string jwt_secret = TEST_JWT_SECRET;
        std::string share_code = "AbCd12";
        uint64_t share_id = 12345;

        auto token1 = TokenService::GenerateShareToken(jwt_secret, share_code, share_id);
        auto token2 = TokenService::GenerateShareToken(jwt_secret, share_code, share_id);

        ASSERT_TRUE(token1.has_value());
        ASSERT_TRUE(token2.has_value());

        auto verify1 = TokenService::VerifyShareToken(jwt_secret, token1.value());
        auto verify2 = TokenService::VerifyShareToken(jwt_secret, token2.value());

        ASSERT_TRUE(verify1.has_value());
        ASSERT_TRUE(verify2.has_value());

        EXPECT_NE(verify1.value().jti, verify2.value().jti) << "Each token should have unique JTI";
    }

    /**
     * @brief 测试不同 share_code 生成不同的令牌
     */
    TEST(TokenServiceShareTest, GenerateShareTokenDifferentShareCodesDifferentTokens) {
        std::string jwt_secret = TEST_JWT_SECRET;
        uint64_t share_id = 12345;

        auto token1 = TokenService::GenerateShareToken(jwt_secret, "code1", share_id);
        auto token2 = TokenService::GenerateShareToken(jwt_secret, "code2", share_id);

        ASSERT_TRUE(token1.has_value());
        ASSERT_TRUE(token2.has_value());
        EXPECT_NE(token1.value(), token2.value()) << "Different share codes should produce different tokens";
    }

    // ================================================================================
    // 撤销令牌负路径断言（Redis 异步路径契约）
    // ================================================================================

    /**
     * @brief 验证静态 VerifyShareToken 不返回 TokenRevoked 错误
     *
     * 契约说明：
     * - 静态方法 VerifyShareToken 仅验证令牌签名、过期时间和 claims
     * - TokenRevoked 错误仅由 VerifyShareTokenWithRedis（异步方法）返回
     * - 撤销检查需要 Redis 连接，静态方法不包含此逻辑
     *
     * 此测试断言：有效的分享令牌在静态验证中不会返回 TokenRevoked
     */
    TEST(TokenServiceShareTest, VerifyShareTokenValidTokenNeverReturnsTokenRevoked) {
        std::string jwt_secret = TEST_JWT_SECRET;
        std::string share_code = "Revoke1";
        uint64_t share_id = 99999;

        auto token_result = TokenService::GenerateShareToken(jwt_secret, share_code, share_id);
        ASSERT_TRUE(token_result.has_value()) << "Token generation should succeed";

        // 静态验证不应返回 TokenRevoked（即使令牌可能已被撤销）
        // 撤销状态检查需要 Redis，属于 VerifyShareTokenWithRedis 的职责
        auto verify_result = TokenService::VerifyShareToken(jwt_secret, token_result.value());

        EXPECT_TRUE(verify_result.has_value()) << "Static verify should succeed for valid token";
        // 如果失败，错误码不应该是 TokenRevoked
        if (!verify_result.has_value()) {
            EXPECT_NE(verify_result.error().code, Code::TokenRevoked)
                << "Static VerifyShareToken should never return TokenRevoked";
        }
    }

    /**
     * @brief 验证 TokenRevoked 错误码的契约定义
     *
     * 此测试确保 TokenRevoked 错误码存在且可用于分享令牌撤销场景。
     * 实际的撤销检查（VerifyShareTokenWithRedis）需要 Redis 环境，
     * 此测试验证错误码本身的定义。
     */
    TEST(TokenServiceShareTest, TokenRevokedErrorCodeContractDefined) {
        // 验证 TokenRevoked 错误码存在
        auto http_status = disk::error::GetHttpStatus(Code::TokenRevoked);
        EXPECT_EQ(http_status, drogon::k401Unauthorized);

        auto message = disk::error::GetErrorMessage(Code::TokenRevoked);
        EXPECT_FALSE(message.empty()) << "TokenRevoked should have an error message";

        // 验证 TokenRevoked 与其他分享令牌错误码不同
        EXPECT_NE(static_cast<uint32_t>(Code::TokenRevoked), static_cast<uint32_t>(Code::TokenMalformed));
        EXPECT_NE(static_cast<uint32_t>(Code::TokenRevoked), static_cast<uint32_t>(Code::TokenExpired));
        EXPECT_NE(static_cast<uint32_t>(Code::TokenRevoked), static_cast<uint32_t>(Code::TokenWrongType));
    }

    // ================================================================================
    // 迁移连续性测试（已统一，此测试已过时）
    // ================================================================================

    /**
     * @brief 迁移连续性测试（已过时）
     *
     * 此测试原本用于验证旧服务生成的分享令牌在新实现中可验证。
     * 由于分享令牌能力已完全统一到 TokenService，此测试不再适用。
     *
     * 统一后的 TokenService 保持完全兼容的分享令牌语义：
     * - issuer = "disk_share"
     * - type = "share"
     * - claims: share_code, share_id (subject), jti
     * - TTL = 3600 秒
     */
    TEST(TokenServiceShareTest, MigrationContinuityLegacyTokenVerifiableByUnified) {
        // 永久跳过 - 旧服务已移除，迁移已完成
        GTEST_SKIP() << "Migration complete: share token APIs unified into TokenService";
    }

    // ================================================================================
    // 分享令牌撤销缓存单元测试
    // ================================================================================

    class ShareRevocationCacheTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            TokenService::Initialize(TEST_JWT_SECRET);
        }

        void SetUp() override {
            m_token_service = TokenService::GetInstance();
            ASSERT_NE(m_token_service, nullptr);
            m_token_service->ClearShareRevocationCache();
        }

        void TearDown() override {
            m_token_service->ClearShareRevocationCache();
        }

        std::shared_ptr<TokenService> m_token_service;
    };

    TEST_F(ShareRevocationCacheTest, CacheInitiallyEmpty) {
        EXPECT_EQ(m_token_service->GetShareRevocationCacheSizeForTest(), 0u);
    }

    TEST_F(ShareRevocationCacheTest, SetCacheEntryIncreasesSize) {
        m_token_service->SetShareRevocationCacheEntryForTest("hash1", false, 5);
        EXPECT_EQ(m_token_service->GetShareRevocationCacheSizeForTest(), 1u);
    }

    TEST_F(ShareRevocationCacheTest, ClearCacheEmptiesAllEntries) {
        m_token_service->SetShareRevocationCacheEntryForTest("hash1", false, 5);
        m_token_service->SetShareRevocationCacheEntryForTest("hash2", true, 10);
        EXPECT_EQ(m_token_service->GetShareRevocationCacheSizeForTest(), 2u);

        m_token_service->ClearShareRevocationCache();
        EXPECT_EQ(m_token_service->GetShareRevocationCacheSizeForTest(), 0u);
    }

    TEST_F(ShareRevocationCacheTest, NegativeCacheTtlIs5Seconds) {
        EXPECT_EQ(TokenService::GetNegativeCacheTtlSeconds(), 5);
    }

    TEST_F(ShareRevocationCacheTest, MultipleEntriesIndependentlyTracked) {
        m_token_service->SetShareRevocationCacheEntryForTest("hash_not_revoked", false, 5);
        m_token_service->SetShareRevocationCacheEntryForTest("hash_revoked", true, 3600);
        EXPECT_EQ(m_token_service->GetShareRevocationCacheSizeForTest(), 2u);
    }

    TEST_F(ShareRevocationCacheTest, SameKeyOverwritesPreviousEntry) {
        m_token_service->SetShareRevocationCacheEntryForTest("hash1", false, 5);
        EXPECT_EQ(m_token_service->GetShareRevocationCacheSizeForTest(), 1u);

        m_token_service->SetShareRevocationCacheEntryForTest("hash1", true, 3600);
        EXPECT_EQ(m_token_service->GetShareRevocationCacheSizeForTest(), 1u);
    }

    // ================================================================================
    // Access/Refresh token generation — instance method tests
    // ================================================================================

    class TokenServiceInstanceTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            TokenService::Initialize(TEST_JWT_SECRET);
        }
    };

    TEST_F(TokenServiceInstanceTest, GenerateTokensProducesVerifiableAccessAndRefresh) {
        auto [access_token, refresh_token] = TokenService::GetInstance()->GenerateTokens(42, "instance_user");

        auto access_result = TokenService::GetInstance()->VerifyAccessToken(access_token);
        ASSERT_TRUE(access_result.has_value());
        EXPECT_EQ(access_result.value().user_id, 42u);
        EXPECT_EQ(access_result.value().username, "instance_user");
        EXPECT_FALSE(access_result.value().jti.empty());

        auto refresh_result = TokenService::GetInstance()->VerifyRefreshToken(refresh_token);
        ASSERT_TRUE(refresh_result.has_value());
        EXPECT_EQ(refresh_result.value().first, 42u);
        EXPECT_FALSE(refresh_result.value().second.empty());
    }

    TEST_F(TokenServiceInstanceTest, GenerateTokensProducesDifferentJtisPerCall) {
        auto [access1, refresh1] = TokenService::GetInstance()->GenerateTokens(1, "user_a");
        auto [access2, refresh2] = TokenService::GetInstance()->GenerateTokens(1, "user_a");

        auto r1 = TokenService::GetInstance()->VerifyAccessToken(access1);
        auto r2 = TokenService::GetInstance()->VerifyAccessToken(access2);
        ASSERT_TRUE(r1.has_value());
        ASSERT_TRUE(r2.has_value());

        EXPECT_NE(r1.value().jti, r2.value().jti);
    }

    TEST_F(TokenServiceInstanceTest, AccessTokenTtlIs7200) {
        EXPECT_EQ(TokenService::GetAccessTokenExpireSeconds(), 7200);
    }

    TEST_F(TokenServiceInstanceTest, RefreshTokenTtlIs604800) {
        EXPECT_EQ(TokenService::GetRefreshTokenExpireSeconds(), 604800);
    }

    TEST_F(TokenServiceInstanceTest, ShareTokenTtlIs3600) {
        EXPECT_EQ(TokenService::GetShareTokenExpireSeconds(), 3600);
    }

} // namespace
