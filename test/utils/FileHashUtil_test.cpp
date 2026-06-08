/**
 * @file FileHashUtil_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件哈希工具测试
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "utils/FileHashUtil.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

using disk::utils::FileHashUtil;

/// ==================== MD5 哈希测试 ====================

TEST(FileHashUtil, HashMd5EmptyString) {
    auto result = FileHashUtil::HashMd5("");
    EXPECT_EQ(result, "d41d8cd98f00b204e9800998ecf8427e");
}

TEST(FileHashUtil, HashMd5KnownString) {
    auto result = FileHashUtil::HashMd5("hello");
    EXPECT_EQ(result, "5d41402abc4b2a76b9719d911017c592");
}

TEST(FileHashUtil, HashMd5LongerString) {
    /// "The quick brown fox jumps over the lazy dog"
    auto result = FileHashUtil::HashMd5("The quick brown fox jumps over the lazy dog");
    EXPECT_EQ(result, "9e107d9d372bb6826bd81d3542a419d6");
}

TEST(FileHashUtil, HashMd5OutputLength) {
    auto result = FileHashUtil::HashMd5("test");
    EXPECT_EQ(result.length(), 32) << "MD5 hash should be 32 hex characters";
}

TEST(FileHashUtil, HashMd5LowerCase) {
    auto result = FileHashUtil::HashMd5("hello");
    /// 验证所有字符均为小写十六进制
    for (char c : result) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
}

/// ==================== SHA256 哈希测试 ====================

TEST(FileHashUtil, HashSha256EmptyString) {
    auto result = FileHashUtil::HashSha256("");
    EXPECT_EQ(result, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(FileHashUtil, HashSha256KnownString) {
    auto result = FileHashUtil::HashSha256("hello");
    EXPECT_EQ(result, "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
}

TEST(FileHashUtil, HashSha256LongerString) {
    /// "The quick brown fox jumps over the lazy dog"
    auto result = FileHashUtil::HashSha256("The quick brown fox jumps over the lazy dog");
    EXPECT_EQ(result, "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
}

TEST(FileHashUtil, HashSha256OutputLength) {
    auto result = FileHashUtil::HashSha256("test");
    EXPECT_EQ(result.length(), 64) << "SHA256 hash should be 64 hex characters";
}

TEST(FileHashUtil, HashSha256LowerCase) {
    auto result = FileHashUtil::HashSha256("hello");
    /// 验证所有字符均为小写十六进制
    for (char c : result) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
}

/// ==================== 哈希验证测试 ====================

TEST(FileHashUtil, VerifyHashCorrect) {
    EXPECT_TRUE(FileHashUtil::VerifyHash("hello", "5d41402abc4b2a76b9719d911017c592"));
}

TEST(FileHashUtil, VerifyHashIncorrect) {
    EXPECT_FALSE(FileHashUtil::VerifyHash("hello", "wrong_hash"));
}

TEST(FileHashUtil, VerifyHashEmptyString) {
    EXPECT_TRUE(FileHashUtil::VerifyHash("", "d41d8cd98f00b204e9800998ecf8427e"));
}

TEST(FileHashUtil, VerifyHashWrongCase) {
    /// MD5 哈希比较应不区分大小写或始终为小写
    EXPECT_FALSE(FileHashUtil::VerifyHash("hello", "5D41402ABC4B2A76B9719D911017C592"));
}

/// ==================== 文件哈希测试 ====================

class FileHashUtilFileTest : public ::testing::Test {
protected:
    std::filesystem::path temp_dir_;

    void SetUp() override {
        /// 创建临时目录
        temp_dir_ = std::filesystem::temp_directory_path() / "disk_test_filehash";
        std::filesystem::create_directories(temp_dir_);
    }

    void TearDown() override {
        /// 清理临时目录
        if (std::filesystem::exists(temp_dir_)) {
            std::filesystem::remove_all(temp_dir_);
        }
    }

    auto CreateTempFile(const std::string& content) -> std::filesystem::path {
        auto file_path = temp_dir_ / ("test_" + std::to_string(std::rand()) + ".txt");
        std::ofstream file(file_path, std::ios::binary);
        file << content;
        file.close();
        return file_path;
    }
};

TEST_F(FileHashUtilFileTest, HashFileMd5Exists) {
    auto file_path = CreateTempFile("hello");
    auto result = FileHashUtil::HashFileMd5(file_path);

    ASSERT_TRUE(result.has_value()) << "Hash should succeed for existing file";
    EXPECT_EQ(*result, "5d41402abc4b2a76b9719d911017c592");
}

TEST_F(FileHashUtilFileTest, HashFileMd5Empty) {
    auto file_path = CreateTempFile("");
    auto result = FileHashUtil::HashFileMd5(file_path);

    ASSERT_TRUE(result.has_value()) << "Hash should succeed for empty file";
    EXPECT_EQ(*result, "d41d8cd98f00b204e9800998ecf8427e");
}

TEST_F(FileHashUtilFileTest, HashFileMd5NotFound) {
    auto result = FileHashUtil::HashFileMd5("/nonexistent/path/file.txt");
    EXPECT_FALSE(result.has_value()) << "Hash should fail for non-existent file";
}

TEST_F(FileHashUtilFileTest, HashFileSha256Exists) {
    auto file_path = CreateTempFile("hello");
    auto result = FileHashUtil::HashFileSha256(file_path);

    ASSERT_TRUE(result.has_value()) << "Hash should succeed for existing file";
    EXPECT_EQ(*result, "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
}

TEST_F(FileHashUtilFileTest, HashFileSha256Empty) {
    auto file_path = CreateTempFile("");
    auto result = FileHashUtil::HashFileSha256(file_path);

    ASSERT_TRUE(result.has_value()) << "Hash should succeed for empty file";
    EXPECT_EQ(*result, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_F(FileHashUtilFileTest, HashFileSha256NotFound) {
    auto result = FileHashUtil::HashFileSha256("/nonexistent/path/file.txt");
    EXPECT_FALSE(result.has_value()) << "Hash should fail for non-existent file";
}

TEST_F(FileHashUtilFileTest, HashFileMd5LargerFile) {
    /// 创建一个 1KB 重复模式的文件
    std::string content(1024, 'A');
    auto file_path = CreateTempFile(content);
    auto result = FileHashUtil::HashFileMd5(file_path);

    ASSERT_TRUE(result.has_value()) << "Hash should succeed for larger file";
    EXPECT_EQ(result->length(), 32) << "MD5 hash should be 32 characters";
}

TEST_F(FileHashUtilFileTest, HashFileSha256LargerFile) {
    /// 创建一个 1KB 重复模式的文件
    std::string content(1024, 'A');
    auto file_path = CreateTempFile(content);
    auto result = FileHashUtil::HashFileSha256(file_path);

    ASSERT_TRUE(result.has_value()) << "Hash should succeed for larger file";
    EXPECT_EQ(result->length(), 64) << "SHA256 hash should be 64 characters";
}

/// ==================== HashFileMd5AndSha256 测试 ====================

TEST_F(FileHashUtilFileTest, HashFileMd5AndSha256Consistent) {
    auto file_path = CreateTempFile("The quick brown fox jumps over the lazy dog");
    auto pair_result = FileHashUtil::HashFileMd5AndSha256(file_path);
    auto md5_result = FileHashUtil::HashFileMd5(file_path);
    auto sha256_result = FileHashUtil::HashFileSha256(file_path);

    ASSERT_TRUE(pair_result.has_value());
    ASSERT_TRUE(md5_result.has_value());
    ASSERT_TRUE(sha256_result.has_value());

    EXPECT_EQ(pair_result->md5, md5_result.value())
        << "MD5 from HashFileMd5AndSha256 should match HashFileMd5";
    EXPECT_EQ(pair_result->sha256, sha256_result.value())
        << "SHA256 from HashFileMd5AndSha256 should match HashFileSha256";
}

TEST_F(FileHashUtilFileTest, HashFileMd5AndSha256EmptyFile) {
    auto file_path = CreateTempFile("");
    auto result = FileHashUtil::HashFileMd5AndSha256(file_path);

    ASSERT_TRUE(result.has_value()) << "Hash should succeed for empty file";
    EXPECT_EQ(result->md5, "d41d8cd98f00b204e9800998ecf8427e");
    EXPECT_EQ(result->sha256, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_F(FileHashUtilFileTest, HashFileMd5AndSha256LargeFile) {
    /// 创建一个 >64KB 的文件来测试缓冲区边界
    std::string content(100 * 1024, 'X');
    auto file_path = CreateTempFile(content);

    auto pair_result = FileHashUtil::HashFileMd5AndSha256(file_path);
    auto md5_result = FileHashUtil::HashFileMd5(file_path);
    auto sha256_result = FileHashUtil::HashFileSha256(file_path);

    ASSERT_TRUE(pair_result.has_value());
    ASSERT_TRUE(md5_result.has_value());
    ASSERT_TRUE(sha256_result.has_value());

    EXPECT_EQ(pair_result->md5, md5_result.value());
    EXPECT_EQ(pair_result->sha256, sha256_result.value());
}

TEST_F(FileHashUtilFileTest, HashFileMd5AndSha256NonexistentFile) {
    auto result = FileHashUtil::HashFileMd5AndSha256("/nonexistent/path/file.txt");
    EXPECT_FALSE(result.has_value()) << "Hash should fail for non-existent file";
}
