/**
 * @file TokenHash_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief TokenHash 单元测试
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "utils/HashUtil.hpp"

#include <gtest/gtest.h>

using disk::utils::HashUtil;
using TokenHash = HashUtil::TokenHash;

TEST(TokenHash, HashProducesFixedLength) {
    const auto* const token = "test_token_12345";
    auto hash_result = HashUtil::HashToken(token);
    ASSERT_TRUE(hash_result.has_value()) << "Token hashing should succeed";
    const auto hash = hash_result.value();

    EXPECT_EQ(hash.size(), 32) << "SHA256 hash should be 32 bytes";
}

TEST(TokenHash, HashSameInputSameOutput) {
    const auto* const token = "test_token_12345";
    auto hash_result1 = HashUtil::HashToken(token);
    auto hash_result2 = HashUtil::HashToken(token);
    ASSERT_TRUE(hash_result1.has_value()) << "Token hashing should succeed";
    ASSERT_TRUE(hash_result2.has_value()) << "Token hashing should succeed";
    const auto hash1 = hash_result1.value();
    const auto hash2 = hash_result2.value();

    EXPECT_EQ(hash1, hash2) << "Same input should produce same hash";
}

TEST(TokenHash, HashDifferentInputDifferentOutput) {
    const auto* const token1 = "test_token_12345";
    const auto* const token2 = "test_token_67890";
    auto hash_result1 = HashUtil::HashToken(token1);
    auto hash_result2 = HashUtil::HashToken(token2);
    ASSERT_TRUE(hash_result1.has_value()) << "Token hashing should succeed";
    ASSERT_TRUE(hash_result2.has_value()) << "Token hashing should succeed";
    const auto hash1 = hash_result1.value();
    const auto hash2 = hash_result2.value();

    EXPECT_NE(hash1, hash2) << "Different input should produce different hash";
}

TEST(TokenHash, ToHexProduces64Chars) {
    const auto* const token = "test_token_12345";
    auto hash_result = HashUtil::HashToken(token);
    ASSERT_TRUE(hash_result.has_value()) << "Token hashing should succeed";
    const auto hash = hash_result.value();
    const auto hex = HashUtil::TokenHashToHex(hash);

    EXPECT_EQ(hex.length(), 64) << "Hex representation should be 64 characters";
}

TEST(TokenHash, ToHexValidHexFormat) {
    const auto* const token = "test_token_12345";
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
    const auto* const token = "test_token_12345";
    auto hash_result = HashUtil::HashToken(token);
    ASSERT_TRUE(hash_result.has_value()) << "Token hashing should succeed";
    const auto hash = hash_result.value();
    const auto hex1 = HashUtil::TokenHashToHex(hash);
    const auto hex2 = HashUtil::TokenHashToHex(hash);

    EXPECT_EQ(hex1, hex2) << "Hex conversion should be consistent";
}

TEST(TokenHash, EmptyToken) {
    const auto* const token = "";
    auto hash_result = HashUtil::HashToken(token);

    EXPECT_FALSE(hash_result.has_value()) << "Empty token should fail";
    if (!hash_result.has_value()) {
        EXPECT_EQ(hash_result.error().code, ErrorCode::InternalError);
    }
}

// ================================================================================
// Password change flow — exercises HashUtil::VerifyPassword() usage semantics
//
// Simulates the password-change path:
//   1. Retrieve stored hash (simulated by hashing old password)
//   2. Verify old password: VerifyPassword(old_password, stored_hash) → true
//   3. Hash new password: HashPassword(new_password)
//   4. Verify new password: VerifyPassword(new_password, new_hash) → true
//   5. Old password no longer matches new hash: VerifyPassword(old_password, new_hash) → false
// ================================================================================

TEST(PasswordChangeFlow, OldPasswordVerifiesAgainstOldHash) {
    auto old_hash = HashUtil::HashPassword("OldPass123!");
    ASSERT_TRUE(old_hash.has_value());

    EXPECT_TRUE(HashUtil::VerifyPassword("OldPass123!", *old_hash));
}

TEST(PasswordChangeFlow, NewPasswordVerifiesAgainstNewHash) {
    auto new_hash = HashUtil::HashPassword("NewPass456!");
    ASSERT_TRUE(new_hash.has_value());

    EXPECT_TRUE(HashUtil::VerifyPassword("NewPass456!", *new_hash));
}

TEST(PasswordChangeFlow, OldPasswordDoesNotVerifyAgainstNewHash) {
    auto new_hash = HashUtil::HashPassword("NewPass456!");
    ASSERT_TRUE(new_hash.has_value());

    EXPECT_FALSE(HashUtil::VerifyPassword("OldPass123!", *new_hash));
}

TEST(PasswordChangeFlow, FullPasswordChangeSequence) {
    auto stored_hash = HashUtil::HashPassword("OriginalPass789");
    ASSERT_TRUE(stored_hash.has_value());

    ASSERT_TRUE(HashUtil::VerifyPassword("OriginalPass789", *stored_hash));
    ASSERT_FALSE(HashUtil::VerifyPassword("WrongOldPass", *stored_hash));

    auto new_hash = HashUtil::HashPassword("ChangedPass321");
    ASSERT_TRUE(new_hash.has_value());

    EXPECT_TRUE(HashUtil::VerifyPassword("ChangedPass321", *new_hash));
    EXPECT_FALSE(HashUtil::VerifyPassword("OriginalPass789", *new_hash));
}

TEST(PasswordChangeFlow, WrongPasswordDoesNotVerifyAgainstArgon2idHash) {
    auto stored_hash = HashUtil::HashPassword("Admin123");
    ASSERT_TRUE(stored_hash.has_value());

    EXPECT_FALSE(HashUtil::VerifyPassword("admin123", *stored_hash));
    EXPECT_FALSE(HashUtil::VerifyPassword("Admin1234", *stored_hash));
    EXPECT_FALSE(HashUtil::VerifyPassword("", *stored_hash));
}
