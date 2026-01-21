/**
 * @file LogoutRequest_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 登出请求测试
 * @version 0.1
 * @date 2026-01-21
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <string>

#include <drogon/HttpRequest.h>
#include <gtest/gtest.h>

#include "dtos/AuthDto.hpp"
#include "services/AuthService.hpp"
#include "services/TokenService.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/Response.hpp"

namespace disk::auth {

    using disk::utils::ConfigMgr;

    // Helper function to create logout request
    static auto CreateLogoutRequest(const std::string& access_token) -> drogon::HttpRequestPtr {
        Json::Value json;
        json["access_token"] = access_token;

        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        std::string body = Json::writeString(builder, json);

        auto req = drogon::HttpRequest::newHttpRequest();
        req->setBody(body);
        req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

        return req;
    }

    // Test: Access token includes JTI
    TEST(TokenService, AccessTokenHasJti) {
        TokenService token_service(
            ConfigMgr::GetInstance()->GetJwtSecret(),
            drogon::app().getRedisClient()
        );

        const auto user_id = 12345;
        const auto username = "testuser";
        auto [access_token, refresh_token] = token_service.GenerateTokens(user_id, username);

        // Verify access token has JTI
        using traits = jwt::traits::open_source_parsers_jsoncpp;
        auto decoded = jwt::decode<traits>(access_token);

        ASSERT_TRUE(decoded.has_payload_claim("jti")) << "Access token should have JTI claim";
        const auto jti = decoded.get_payload_claim("jti").as_string();
        EXPECT_FALSE(jti.empty()) << "JTI should not be empty";

        // Verify refresh token also has JTI
        auto refresh_decoded = jwt::decode<traits>(refresh_token);
        ASSERT_TRUE(refresh_decoded.has_payload_claim("jti")) << "Refresh token should have JTI claim";
    }

    // Test: Successful logout
    TEST(TokenService, InvalidateAccessTokenSuccess) {
        TokenService token_service(
            ConfigMgr::GetInstance()->GetJwtSecret(),
            drogon::app().getRedisClient()
        );

        const auto user_id = 12345;
        const auto username = "testuser";
        auto [access_token, refresh_token] = token_service.GenerateTokens(user_id, username);

        // Invalidate token
        auto invalidate_result = co_await token_service.InvalidateAccessToken(access_token);
        ASSERT_TRUE(invalidate_result) << "Token invalidation should succeed";

        // Verify token is blacklisted
        using traits = jwt::traits::open_source_parsers_jsoncpp;
        auto decoded = jwt::decode<traits>(access_token);
        const auto jti = decoded.get_payload_claim("jti").as_string();

        auto is_revoked = co_await token_service.IsAccessTokenRevoked(jti);
        EXPECT_TRUE(is_revoked) << "Token should be blacklisted after logout";
    }

    // Test: Logout with invalid token
    TEST(TokenService, InvalidateAccessTokenInvalidToken) {
        TokenService token_service(
            ConfigMgr::GetInstance()->GetJwtSecret(),
            drogon::app().getRedisClient()
        );

        const auto invalid_token = "not-a-valid-jwt-token";

        auto invalidate_result = co_await token_service.InvalidateAccessToken(invalid_token);
        EXPECT_FALSE(invalidate_result) << "Invalid token should fail invalidation";
    }

    // Test: Refresh token revocation
    TEST(TokenService, RevokeRefreshTokenSuccess) {
        TokenService token_service(
            ConfigMgr::GetInstance()->GetJwtSecret(),
            drogon::app().getRedisClient()
        );

        const auto user_id = 12345;

        // Store a refresh token first
        auto [access_token, _] = token_service.GenerateTokens(user_id, "testuser");
        auto store_result = co_await token_service.StoreRefreshToken(user_id, access_token);
        ASSERT_TRUE(store_result) << "Refresh token storage should succeed";

        // Revoke refresh token
        auto revoke_result = co_await token_service.RevokeRefreshToken(user_id);
        ASSERT_TRUE(revoke_result) << "Refresh token revocation should succeed";

        // Verify refresh token is deleted from Redis
        auto redis = drogon::app().getRedisClient();
        const auto key = "refresh_token:" + std::to_string(user_id);
        auto result = co_await redis->execCommandCoro("EXISTS %s", key.c_str());
        const auto exists = result.asInteger();

        EXPECT_EQ(exists, 0) << "Refresh token should be deleted from Redis";
    }

    // Test: Revoke non-existent refresh token (idempotent)
    TEST(TokenService, RevokeRefreshTokenNonExistent) {
        TokenService token_service(
            ConfigMgr::GetInstance()->GetJwtSecret(),
            drogon::app().getRedisClient()
        );

        const auto user_id = 12345;

        // Revoke refresh token that doesn't exist
        auto revoke_result = co_await token_service.RevokeRefreshToken(user_id);
        ASSERT_TRUE(revoke_result) << "Revoking non-existent token should succeed (idempotent)";
    }

    // Test: TTL calculation for fresh token
    TEST(TokenService, CalculateTtlForFreshToken) {
        TokenService token_service(
            ConfigMgr::GetInstance()->GetJwtSecret(),
            drogon::app().getRedisClient()
        );

        const auto user_id = 12345;
        const auto username = "testuser";
        auto [access_token, refresh_token] = token_service.GenerateTokens(user_id, username);

        // Calculate TTL for fresh access token
        auto ttl_result = token_service.CalculateRemainingTtl(access_token);
        ASSERT_TRUE(ttl_result.has_value()) << "TTL calculation should succeed";

        const auto ttl = ttl_result.value();
        EXPECT_GT(ttl, 7000) << "Fresh token should have ~7200s TTL";
        EXPECT_LE(ttl, 7200) << "TTL should not exceed 7200s";
    }

    // Test: TTL calculation for expired token
    TEST(TokenService, CalculateTtlForExpiredToken) {
        TokenService token_service(
            ConfigMgr::GetInstance()->GetJwtSecret(),
            drogon::app().getRedisClient()
        );

        // Use a manually created expired token
        const auto expired_token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJleHAiOiJhdXQiOjZ2R2FmTkFsoy4.eyJleGUiOiJKZ1JlOiJhbGciOiJIUzI1NiJ9.eyJleGUiOiJKZ1JlOiJhbGciOiJIUzI1NiJ9.eyJleGUiOiJKZ1JlOiJhbGciOiJIUzI1NiJ9.sign";

        auto ttl_result = token_service.CalculateRemainingTtl(expired_token);
        EXPECT_FALSE(ttl_result.has_value()) << "Expired token should fail TTL calculation";
    }

    // Test: Idempotent logout (multiple attempts with same token)
    TEST(TokenService, InvalidateAccessTokenIdempotent) {
        TokenService token_service(
            ConfigMgr::GetInstance()->GetJwtSecret(),
            drogon::app().getRedisClient()
        );

        const auto user_id = 12345;
        const auto username = "testuser";
        auto [access_token, refresh_token] = token_service.GenerateTokens(user_id, username);

        // First logout
        auto first_result = co_await token_service.InvalidateAccessToken(access_token);
        ASSERT_TRUE(first_result) << "First logout should succeed";

        // Second logout with same token (idempotent)
        auto second_result = co_await token_service.InvalidateAccessToken(access_token);
        EXPECT_TRUE(second_result) << "Second logout should also succeed (idempotent)";
    }

} // namespace disk::auth
