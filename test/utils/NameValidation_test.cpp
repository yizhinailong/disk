/**
 * @file NameValidation_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Name validation utility tests
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "utils/NameValidation.hpp"

#include <string>

#include <gtest/gtest.h>

using disk::utils::HasForbiddenDriveItemChars;
using disk::utils::IsValidUtf8WithoutControlChars;

TEST(NameValidation, AcceptsAsciiAndUtf8Names) {
    EXPECT_TRUE(IsValidUtf8WithoutControlChars("document.pdf"));
    EXPECT_TRUE(IsValidUtf8WithoutControlChars("毕业论文_最终版.doc"));
    EXPECT_TRUE(IsValidUtf8WithoutControlChars("📁资料"));
    EXPECT_TRUE(IsValidUtf8WithoutControlChars("café.txt"));
}

TEST(NameValidation, RejectsMalformedUtf8) {
    std::string overlong;
    overlong.push_back(static_cast<char>(0xC0));
    overlong.push_back(static_cast<char>(0xAF));
    EXPECT_FALSE(IsValidUtf8WithoutControlChars(overlong));

    std::string bad_continuation;
    bad_continuation.push_back(static_cast<char>(0xE2));
    bad_continuation.push_back(static_cast<char>(0x28));
    bad_continuation.push_back(static_cast<char>(0xA1));
    EXPECT_FALSE(IsValidUtf8WithoutControlChars(bad_continuation));
}

TEST(NameValidation, RejectsControlCharacters) {
    EXPECT_FALSE(IsValidUtf8WithoutControlChars("name\x01.txt"));
    EXPECT_FALSE(IsValidUtf8WithoutControlChars("name\x7F.txt"));
    EXPECT_FALSE(IsValidUtf8WithoutControlChars("name\xC2\x85.txt"));
}

TEST(NameValidation, DetectsForbiddenFilesystemCharacters) {
    EXPECT_FALSE(HasForbiddenDriveItemChars("毕业论文.doc"));
    EXPECT_TRUE(HasForbiddenDriveItemChars("dir/file.txt"));
    EXPECT_TRUE(HasForbiddenDriveItemChars("dir\\file.txt"));
    EXPECT_TRUE(HasForbiddenDriveItemChars("bad:name.txt"));
    EXPECT_TRUE(HasForbiddenDriveItemChars("bad\x01name.txt"));
}
