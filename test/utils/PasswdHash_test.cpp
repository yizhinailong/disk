/**
 * @file PasswdHash_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 密码哈希工具测试
 * @version 0.1
 * @date 2026-01-17
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "utils/PasswdHash.hpp"

#include <string>

#include <gtest/gtest.h>
#include <sodium.h>

using disk::utils::passwd::Hash;
using disk::utils::passwd::Verify;

TEST(PasswdHash, HashValidPassword) {
    std::string password = "SecurePass123";
    auto result = Hash(password);
    ASSERT_TRUE(result.has_value()) << "Password hashing should succeed";
    EXPECT_FALSE(result->empty()) << "Hashed password should not be empty";
    EXPECT_TRUE(result->find("$argon2id$") == 0) << "Hash should use Argon2id algorithm";
}

TEST(PasswdHash, HashSamePasswordDifferentHash) {
    std::string password = "TestPass456";
    auto hash1 = Hash(password);
    auto hash2 = Hash(password);
    ASSERT_TRUE(hash1.has_value());
    ASSERT_TRUE(hash2.has_value());
    EXPECT_NE(*hash1, *hash2) << "Same password should produce different hashes (different salts)";
}

TEST(PasswdHash, EmptyPassword) {
    std::string password;
    auto result = Hash(password);
    EXPECT_FALSE(result.has_value()) << "Empty password should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InternalError);
    }
}

TEST(PasswdHash, HashMinLengthPassword) {
    std::string password = "Test1234";
    auto result = Hash(password);
    ASSERT_TRUE(result.has_value()) << "Minimum length password (8 chars) should hash successfully";
}

TEST(PasswdHash, HashMaxLengthPassword) {
    std::string password = "Test" + std::string(60, '1');
    ASSERT_EQ(password.length(), 64) << "Constructed max length password";
    auto result = Hash(password);
    ASSERT_TRUE(result.has_value()) << "Maximum length password (64 chars) should hash successfully";
}

TEST(PasswdHash, VerifyCorrectPassword) {
    std::string password = "CorrectPass789";
    auto hash_result = Hash(password);
    ASSERT_TRUE(hash_result.has_value()) << "Password hashing should succeed";

    bool is_valid = Verify(password, *hash_result);
    EXPECT_TRUE(is_valid) << "Correct password should verify successfully";
}

TEST(PasswdHash, VerifyWrongPassword) {
    std::string password = "CorrectPass789";
    auto hash_result = Hash(password);
    ASSERT_TRUE(hash_result.has_value()) << "Password hashing should succeed";

    bool is_valid = Verify("WrongPass123", *hash_result);
    EXPECT_FALSE(is_valid) << "Wrong password should fail verification";
}

TEST(PasswdHash, VerifyInvalidHash) {
    std::string password = "TestPass123";
    std::string invalid_hash = "invalid_hash_format";
    bool is_valid = Verify(password, invalid_hash);
    EXPECT_FALSE(is_valid) << "Invalid hash format should fail verification";
}

TEST(PasswdHash, HashAndVerifyRoundTrip) {
    std::string password = "RoundTripTest456";

    auto hash_result = Hash(password);
    ASSERT_TRUE(hash_result.has_value()) << "Hashing should succeed";

    bool is_valid = Verify(password, *hash_result);
    EXPECT_TRUE(is_valid) << "Password should verify against its own hash";
}
