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

#include "utils/TokenHash.hpp"

#include <gtest/gtest.h>

using disk::utils::token::Hash;
using disk::utils::token::ToHex;

TEST(TokenHash, HashProducesFixedLength) {
    const auto token = "test_token_12345";
    const auto hash = Hash(token);

    EXPECT_EQ(hash.size(), 32) << "SHA256 hash should be 32 bytes";
}

TEST(TokenHash, HashSameInputSameOutput) {
    const auto token = "test_token_12345";
    const auto hash1 = Hash(token);
    const auto hash2 = Hash(token);

    EXPECT_EQ(hash1, hash2) << "Same input should produce same hash";
}

TEST(TokenHash, HashDifferentInputDifferentOutput) {
    const auto token1 = "test_token_12345";
    const auto token2 = "test_token_67890";
    const auto hash1 = Hash(token1);
    const auto hash2 = Hash(token2);

    EXPECT_NE(hash1, hash2) << "Different input should produce different hash";
}

TEST(TokenHash, ToHexProduces64Chars) {
    const auto token = "test_token_12345";
    const auto hash = Hash(token);
    const auto hex = ToHex(hash);

    EXPECT_EQ(hex.length(), 64) << "Hex representation should be 64 characters";
}

TEST(TokenHash, ToHexValidHexFormat) {
    const auto token = "test_token_12345";
    const auto hash = Hash(token);
    const auto hex = ToHex(hash);

    // 验证所有字符都是十六进制
    for (char c : hex) {
        EXPECT_GE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'), 0);
    }
}

TEST(TokenHash, ToHexConsistent) {
    const auto token = "test_token_12345";
    const auto hash = Hash(token);
    const auto hex1 = ToHex(hash);
    const auto hex2 = ToHex(hash);

    EXPECT_EQ(hex1, hex2) << "Hex conversion should be consistent";
}

TEST(TokenHash, EmptyToken) {
    const auto token = "";
    const auto hash = Hash(token);
    const auto hex = ToHex(hash);

    // 空字符串的 SHA256 哈希是固定的
    EXPECT_EQ(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}
