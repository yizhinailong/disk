/**
 * @file TokenHash_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief TokenHash 单元测试
 * @version 0.1
 * @date 2026-01-21
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <gtest/gtest.h>

#include "utils/HashUtil.hpp"

using disk::utils::HashUtil;
using TokenHash = HashUtil::TokenHash;

TEST(TokenHash, HashProducesFixedLength) {
    const auto token = "test_token_12345";
    auto hash_result = HashUtil::HashToken(token);
    ASSERT_TRUE(hash_result.has_value()) << "Token hashing should succeed";
    const auto hash = hash_result.value();

    EXPECT_EQ(hash.size(), 32) << "SHA256 hash should be 32 bytes";
}

TEST(TokenHash, HashSameInputSameOutput) {
    const auto token = "test_token_12345";
    auto hash_result1 = HashUtil::HashToken(token);
    auto hash_result2 = HashUtil::HashToken(token);
    ASSERT_TRUE(hash_result1.has_value()) << "Token hashing should succeed";
    ASSERT_TRUE(hash_result2.has_value()) << "Token hashing should succeed";
    const auto hash1 = hash_result1.value();
    const auto hash2 = hash_result2.value();

    EXPECT_EQ(hash1, hash2) << "Same input should produce same hash";
}

TEST(TokenHash, HashDifferentInputDifferentOutput) {
    const auto token1 = "test_token_12345";
    const auto token2 = "test_token_67890";
    auto hash_result1 = HashUtil::HashToken(token1);
    auto hash_result2 = HashUtil::HashToken(token2);
    ASSERT_TRUE(hash_result1.has_value()) << "Token hashing should succeed";
    ASSERT_TRUE(hash_result2.has_value()) << "Token hashing should succeed";
    const auto hash1 = hash_result1.value();
    const auto hash2 = hash_result2.value();

    EXPECT_NE(hash1, hash2) << "Different input should produce different hash";
}

TEST(TokenHash, ToHexProduces64Chars) {
    const auto token = "test_token_12345";
    auto hash_result = HashUtil::HashToken(token);
    ASSERT_TRUE(hash_result.has_value()) << "Token hashing should succeed";
    const auto hash = hash_result.value();
    const auto hex = HashUtil::TokenHashToHex(hash);

    EXPECT_EQ(hex.length(), 64) << "Hex representation should be 64 characters";
}

TEST(TokenHash, ToHexValidHexFormat) {
    const auto token = "test_token_12345";
    auto hash_result = HashUtil::HashToken(token);
    ASSERT_TRUE(hash_result.has_value()) << "Token hashing should succeed";
    const auto hash = hash_result.value();
    const auto hex = HashUtil::TokenHashToHex(hash);

    // 验证所有字符都是十六进制
    for (char c : hex) {
        EXPECT_GE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'), 0);
    }
}

TEST(TokenHash, ToHexConsistent) {
    const auto token = "test_token_12345";
    auto hash_result = HashUtil::HashToken(token);
    ASSERT_TRUE(hash_result.has_value()) << "Token hashing should succeed";
    const auto hash = hash_result.value();
    const auto hex1 = HashUtil::TokenHashToHex(hash);
    const auto hex2 = HashUtil::TokenHashToHex(hash);

    EXPECT_EQ(hex1, hex2) << "Hex conversion should be consistent";
}

TEST(TokenHash, EmptyToken) {
    const auto token = "";
    auto hash_result = HashUtil::HashToken(token);

    EXPECT_FALSE(hash_result.has_value()) << "Empty token should fail";
    if (!hash_result.has_value()) {
        EXPECT_EQ(hash_result.error().code, ErrorCode::InternalError);
    }
}
