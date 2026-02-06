/**
 * @file UserDto_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief User DTO unit tests
 * @version 0.1
 * @date 2026-02-06
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "dtos/UserDto.hpp"

#include <string>

#include <drogon/HttpRequest.h>
#include <drogon/utils/Utilities.h>
#include <gtest/gtest.h>
#include <json/json.h>

#include "utils/ErrorCode.hpp"

using disk::user::ChangePasswordRequest;

static auto CreateChangePasswordRequest(
    const std::string& old_password,
    const std::string& new_password
) -> drogon::HttpRequestPtr {
    Json::Value json;
    json["old_password"] = old_password;
    json["new_password"] = new_password;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    return req;
}

TEST(ChangePasswordRequest, ValidParameters) {
    auto req = CreateChangePasswordRequest("OldPass123", "NewPass456");
    auto result = ChangePasswordRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid parameters should pass validation";
    EXPECT_EQ(result->old_password, "OldPass123");
    EXPECT_EQ(result->new_password, "NewPass456");
}

TEST(ChangePasswordRequest, MissingOldPassword) {
    Json::Value json;
    json["new_password"] = "NewPass456";

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto result = ChangePasswordRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Missing old_password should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ChangePasswordRequest, MissingNewPassword) {
    Json::Value json;
    json["old_password"] = "OldPass123";

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto result = ChangePasswordRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Missing new_password should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ChangePasswordRequest, OldPasswordWrongType) {
    Json::Value json;
    json["old_password"] = 123;
    json["new_password"] = "NewPass456";

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto result = ChangePasswordRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Old password with wrong type should fail";
}

TEST(ChangePasswordRequest, NewPasswordWrongType) {
    Json::Value json;
    json["old_password"] = "OldPass123";
    json["new_password"] = 456;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto result = ChangePasswordRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "New password with wrong type should fail";
}

TEST(ChangePasswordRequest, NewPasswordTooShort) {
    auto req = CreateChangePasswordRequest("OldPass123", "New1234");
    auto result = ChangePasswordRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "New password with 7 characters should fail";
}

TEST(ChangePasswordRequest, NewPasswordTooLong) {
    std::string new_password(65, 'A');
    auto req = CreateChangePasswordRequest("OldPass123", new_password);
    auto result = ChangePasswordRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "New password with 65 characters should fail";
}

TEST(ChangePasswordRequest, NewPasswordWithoutUppercase) {
    auto req = CreateChangePasswordRequest("OldPass123", "newpass123");
    auto result = ChangePasswordRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "New password without uppercase should fail";
}

TEST(ChangePasswordRequest, NewPasswordWithoutLowercase) {
    auto req = CreateChangePasswordRequest("OldPass123", "NEWPASS123");
    auto result = ChangePasswordRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "New password without lowercase should fail";
}

TEST(ChangePasswordRequest, NewPasswordWithoutDigit) {
    auto req = CreateChangePasswordRequest("OldPass123", "NewPassword");
    auto result = ChangePasswordRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "New password without digit should fail";
}

TEST(ChangePasswordRequest, SameAsOldPassword) {
    auto req = CreateChangePasswordRequest("SamePass123", "SamePass123");
    auto result = ChangePasswordRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Same new password as old password should fail";
}

TEST(ChangePasswordRequest, InvalidJSON) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody("{invalid json}");
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto result = ChangePasswordRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Invalid JSON should fail";
}

TEST(ChangePasswordRequest, NewPasswordValid8Chars) {
    auto req = CreateChangePasswordRequest("OldPass123", "New12345");
    auto result = ChangePasswordRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "New password with 8 characters should pass";
    EXPECT_EQ(result->new_password, "New12345");
}

TEST(ChangePasswordRequest, NewPasswordValid64Chars) {
    std::string new_password = "Test" + std::string(60, '1');
    auto req = CreateChangePasswordRequest("OldPass123", new_password);
    auto result = ChangePasswordRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "New password with 64 characters should pass";
    EXPECT_EQ(result->new_password.length(), 64);
}

TEST(ChangePasswordRequest, NewPasswordValidComplex) {
    auto req = CreateChangePasswordRequest("OldPass123", "SecurePass456");
    auto result = ChangePasswordRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Complex new password should pass";
    EXPECT_EQ(result->new_password, "SecurePass456");
}
