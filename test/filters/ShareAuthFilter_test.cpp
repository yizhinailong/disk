/**
 * @file ShareAuthFilter_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief ShareAuthFilter 单元测试
 * @version 0.1
 * @date 2026-02-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <string>

#include <gtest/gtest.h>

#include "services/ShareTokenService.hpp"
#include "utils/ErrorCode.hpp"

namespace {

    using disk::error::Code;
    using disk::services::ShareTokenService;

    static constexpr const char* TEST_JWT_SECRET = "test_secret_key_for_share_token_32b";

    TEST(ShareAuthFilterTest, ErrorMappingContract_TokenMissing) {
        auto http_status = disk::error::GetHttpStatus(Code::TokenMissing);
        EXPECT_EQ(http_status, drogon::k401Unauthorized);
        auto message = disk::error::GetErrorMessage(Code::TokenMissing);
        EXPECT_EQ(message, std::string("未提供令牌"));
    }

    TEST(ShareAuthFilterTest, ErrorMappingContract_TokenMalformed) {
        auto http_status = disk::error::GetHttpStatus(Code::TokenMalformed);
        EXPECT_EQ(http_status, drogon::k401Unauthorized);
        auto message = disk::error::GetErrorMessage(Code::TokenMalformed);
        EXPECT_EQ(message, std::string("令牌格式错误"));
    }

    TEST(ShareAuthFilterTest, ErrorMappingContract_TokenExpired) {
        auto http_status = disk::error::GetHttpStatus(Code::TokenExpired);
        EXPECT_EQ(http_status, drogon::k401Unauthorized);
        auto message = disk::error::GetErrorMessage(Code::TokenExpired);
        EXPECT_EQ(message, std::string("令牌已过期"));
    }

    TEST(ShareAuthFilterTest, ErrorMappingContract_TokenRevoked) {
        auto http_status = disk::error::GetHttpStatus(Code::TokenRevoked);
        EXPECT_EQ(http_status, drogon::k401Unauthorized);
        auto message = disk::error::GetErrorMessage(Code::TokenRevoked);
        EXPECT_EQ(message, std::string("令牌已被注销"));
    }

    TEST(ShareAuthFilterTest, ErrorCodesAreDistinct) {
        EXPECT_NE(static_cast<uint32_t>(Code::TokenMissing), static_cast<uint32_t>(Code::TokenMalformed));
        EXPECT_NE(static_cast<uint32_t>(Code::TokenMissing), static_cast<uint32_t>(Code::TokenExpired));
        EXPECT_NE(static_cast<uint32_t>(Code::TokenMissing), static_cast<uint32_t>(Code::TokenRevoked));
        EXPECT_NE(static_cast<uint32_t>(Code::TokenMalformed), static_cast<uint32_t>(Code::TokenExpired));
        EXPECT_NE(static_cast<uint32_t>(Code::TokenMalformed), static_cast<uint32_t>(Code::TokenRevoked));
        EXPECT_NE(static_cast<uint32_t>(Code::TokenExpired), static_cast<uint32_t>(Code::TokenRevoked));
    }

    TEST(ShareAuthFilterTest, ServiceVerifyToken_MalformedPath_ReturnsMalformedCode) {
        auto result = ShareTokenService::VerifyToken(TEST_JWT_SECRET, "malformed.token.string");
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, Code::TokenMalformed);
    }

    TEST(ShareAuthFilterTest, ServiceVerifyToken_EmptyPath_ReturnsMalformedCode) {
        auto result = ShareTokenService::VerifyToken(TEST_JWT_SECRET, "");
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, Code::TokenMalformed);
    }

    TEST(ShareAuthFilterTest, DISABLED_FilterMissingToken_ReturnsTokenMissing) {
        SUCCEED() << "Filter integration test requires Drogon runtime with HTTP request context";
    }

    TEST(ShareAuthFilterTest, DISABLED_FilterRevokedToken_ReturnsTokenRevoked) {
        SUCCEED() << "Filter revoked test requires Drogon runtime with Redis connection";
    }

    TEST(ShareAuthFilterTest, DISABLED_FilterValidToken_SetsRequestAttributes) {
        SUCCEED() << "Filter attribute test requires Drogon runtime with HTTP request context";
    }

} // namespace
