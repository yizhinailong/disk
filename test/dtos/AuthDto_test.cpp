/**
 * @file AuthDto_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Auth DTO unit tests
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "dtos/AuthDto.hpp"

#include <string>

#include <drogon/HttpRequest.h>
#include <drogon/utils/Utilities.h>
#include <gtest/gtest.h>
#include <json/json.h>

#include "utils/ErrorCode.hpp"

using disk::auth::LoginRequest;
using disk::auth::LoginResponse;
using disk::auth::RefreshTokenRequest;
using disk::auth::RefreshTokenResponse;
using disk::auth::RegisterRequest;
using disk::auth::RegisterResponse;

static auto CreateRegisterRequest(
    const std::string& username,
    const std::string& email,
    const std::string& password
) -> drogon::HttpRequestPtr {
    Json::Value json;
    json["username"] = username;
    json["email"] = email;
    json["password"] = password;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    return req;
}

TEST(RegisterRequest, ValidParameters) {
    auto req = CreateRegisterRequest("test_user", "test@example.com", "TestPass123");
    auto result = RegisterRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid parameters should pass validation";
    EXPECT_EQ(result->username, "test_user");
    EXPECT_EQ(result->email, "test@example.com");
    EXPECT_EQ(result->password, "TestPass123");
}

TEST(RegisterRequest, UsernameTooShort) {
    auto req = CreateRegisterRequest("abc", "test@example.com", "TestPass123");
    auto result = RegisterRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Username with 3 characters should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(RegisterRequest, UsernameValid4Chars) {
    auto req = CreateRegisterRequest("abcd", "test@example.com", "TestPass123");
    auto result = RegisterRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Username with 4 characters should pass";
    EXPECT_EQ(result->username, "abcd");
}

TEST(RegisterRequest, UsernameValid32Chars) {
    std::string username(32, 'a');
    auto req = CreateRegisterRequest(username, "test@example.com", "TestPass123");
    auto result = RegisterRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Username with 32 characters should pass";
    EXPECT_EQ(result->username.length(), 32);
}

TEST(RegisterRequest, UsernameTooLong) {
    std::string username(33, 'a');
    auto req = CreateRegisterRequest(username, "test@example.com", "TestPass123");
    auto result = RegisterRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Username with 33 characters should fail";
}

TEST(RegisterRequest, UsernameInvalidSpecialChars) {
    auto req = CreateRegisterRequest("user@name", "test@example.com", "TestPass123");
    auto result = RegisterRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Username with special chars should fail";
}

TEST(RegisterRequest, UsernameInvalidSpaces) {
    auto req = CreateRegisterRequest("user name", "test@example.com", "TestPass123");
    auto result = RegisterRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Username with spaces should fail";
}

TEST(RegisterRequest, UsernameInvalidHyphen) {
    auto req = CreateRegisterRequest("user-name", "test@example.com", "TestPass123");
    auto result = RegisterRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Username with hyphen should fail";
}

TEST(RegisterRequest, EmailValidFormat) {
    auto req = CreateRegisterRequest("testuser", "test@example.com", "TestPass123");
    auto result = RegisterRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid email format should pass";
    EXPECT_EQ(result->email, "test@example.com");
}

TEST(RegisterRequest, EmailInvalidNoAtSymbol) {
    auto req = CreateRegisterRequest("testuser", "testexample.com", "TestPass123");
    auto result = RegisterRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Email without @ should fail";
}

TEST(RegisterRequest, EmailInvalidNoDomain) {
    auto req = CreateRegisterRequest("testuser", "test@", "TestPass123");
    auto result = RegisterRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Email without domain should fail";
}

TEST(RegisterRequest, EmailInvalidNoUsername) {
    auto req = CreateRegisterRequest("testuser", "@example.com", "TestPass123");
    auto result = RegisterRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Email without username should fail";
}

TEST(RegisterRequest, PasswordTooShort) {
    auto req = CreateRegisterRequest("testuser", "test@example.com", "Test123");
    auto result = RegisterRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Password with 7 characters should fail";
}

TEST(RegisterRequest, PasswordValid8Chars) {
    auto req = CreateRegisterRequest("testuser", "test@example.com", "Test1234");
    auto result = RegisterRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Password with 8 characters should pass";
    EXPECT_EQ(result->password, "Test1234");
}

TEST(RegisterRequest, PasswordValid64Chars) {
    std::string password = "Test" + std::string(60, '1');
    auto req = CreateRegisterRequest("testuser", "test@example.com", password);
    auto result = RegisterRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Password with 64 characters should pass";
    EXPECT_EQ(result->password.length(), 64);
}

TEST(RegisterRequest, PasswordTooLong) {
    std::string password(65, 'A');
    auto req = CreateRegisterRequest("testuser", "test@example.com", password);
    auto result = RegisterRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Password with 65 characters should fail";
}

TEST(RegisterRequest, PasswordWithoutUppercase) {
    auto req = CreateRegisterRequest("testuser", "test@example.com", "password123");
    auto result = RegisterRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Password without uppercase should fail";
}

TEST(RegisterRequest, PasswordWithoutLowercase) {
    auto req = CreateRegisterRequest("testuser", "test@example.com", "PASSWORD123");
    auto result = RegisterRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Password without lowercase should fail";
}

TEST(RegisterRequest, PasswordWithoutDigit) {
    auto req = CreateRegisterRequest("testuser", "test@example.com", "PasswordOnly");
    auto result = RegisterRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Password without digit should fail";
}

TEST(RegisterRequest, PasswordValidComplex) {
    auto req = CreateRegisterRequest("testuser", "test@example.com", "SecurePass456");
    auto result = RegisterRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Complex password should pass";
    EXPECT_EQ(result->password, "SecurePass456");
}

TEST(RegisterRequest, MissingUsername) {
    Json::Value json;
    json["email"] = "test@example.com";
    json["password"] = "TestPass123";

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto result = RegisterRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Missing username should fail";
}

TEST(RegisterRequest, MissingEmail) {
    Json::Value json;
    json["username"] = "testuser";
    json["password"] = "TestPass123";

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto result = RegisterRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Missing email should fail";
}

TEST(RegisterRequest, MissingPassword) {
    Json::Value json;
    json["username"] = "testuser";
    json["email"] = "test@example.com";

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto result = RegisterRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Missing password should fail";
}

TEST(RegisterRequest, InvalidJSON) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody("{invalid json}");
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto result = RegisterRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Invalid JSON should fail";
}

static auto CreateLoginRequest(
    const std::string& account,
    const std::string& password
) -> drogon::HttpRequestPtr {
    Json::Value json;
    json["account"] = account;
    json["password"] = password;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    return req;
}

TEST(LoginRequest, ValidUsernamePassword) {
    auto req = CreateLoginRequest("test_user", "SecurePass123");
    auto result = LoginRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid username and password should pass";
    EXPECT_EQ(result->account, "test_user");
    EXPECT_EQ(result->password, "SecurePass123");
}

TEST(LoginRequest, ValidEmailPassword) {
    auto req = CreateLoginRequest("test@example.com", "SecurePass123");
    auto result = LoginRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid email and password should pass";
    EXPECT_EQ(result->account, "test@example.com");
    EXPECT_EQ(result->password, "SecurePass123");
}

TEST(LoginRequest, MissingAccount) {
    Json::Value json;
    json["password"] = "SecurePass123";

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto result = LoginRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Missing account should fail";
}

TEST(LoginRequest, MissingPassword) {
    Json::Value json;
    json["account"] = "test_user";

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto result = LoginRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Missing password should fail";
}

TEST(LoginRequest, EmptyAccount) {
    auto req = CreateLoginRequest("", "SecurePass123");
    auto result = LoginRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Empty account should fail";
}

TEST(LoginRequest, EmptyPassword) {
    auto req = CreateLoginRequest("test_user", "");
    auto result = LoginRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Empty password should fail";
}

TEST(LoginRequest, InvalidJSON) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody("{invalid json}");
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto result = LoginRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Invalid JSON should fail";
}

TEST(LoginRequest, AccountWrongType) {
    Json::Value json;
    json["account"] = 123;
    json["password"] = "SecurePass123";

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto result = LoginRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Account with wrong type should fail";
}

TEST(LoginRequest, PasswordWrongType) {
    Json::Value json;
    json["account"] = "test_user";
    json["password"] = 123;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto result = LoginRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Password with wrong type should fail";
}

static auto CreateRefreshTokenRequest(const std::string& refresh_token) -> drogon::HttpRequestPtr {
    Json::Value json;
    json["refresh_token"] = refresh_token;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    return req;
}

TEST(RefreshTokenRequest, ValidToken) {
    std::string token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIiwidHlwZSI6InJlZnJlc2gifQ.signature";
    auto req = CreateRefreshTokenRequest(token);
    auto result = RefreshTokenRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid refresh token should pass validation";
    EXPECT_EQ(result->refresh_token, token);
}

TEST(RefreshTokenRequest, MissingRefreshToken) {
    Json::Value json;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto result = RefreshTokenRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Missing refresh_token should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(RefreshTokenRequest, InvalidJSON) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody("{invalid json}");
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto result = RefreshTokenRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Invalid JSON should fail";
}

TEST(RegisterResponse, ToJsonCorrectFields) {
    RegisterResponse response;
    response.id = 12345;
    response.username = "test_user";
    response.email = "test@example.com";
    response.nickname = "test_user";
    response.storage_quota = 10737418240;
    response.storage_used = 0;

    auto json = response.ToJson();

    EXPECT_EQ(json["id"].asUInt64(), 12345);
    EXPECT_EQ(json["username"].asString(), "test_user");
    EXPECT_EQ(json["email"].asString(), "test@example.com");
    EXPECT_EQ(json["nickname"].asString(), "test_user");
    EXPECT_EQ(json["storage_quota"].asUInt64(), 10737418240);
    EXPECT_EQ(json["storage_used"].asUInt64(), 0);
}

TEST(LoginResponse, ToJsonCorrectFields) {
    LoginResponse response;
    response.access_token = "test_access_token_123";
    response.refresh_token = "test_refresh_token_456";
    response.expires_in = 7200;
    response.token_type = "Bearer";
    response.user.id = 12345;
    response.user.username = "test_user";
    response.user.email = "test@example.com";

    auto json = response.ToJson();

    EXPECT_EQ(json["access_token"].asString(), "test_access_token_123");
    EXPECT_EQ(json["refresh_token"].asString(), "test_refresh_token_456");
    EXPECT_EQ(json["expires_in"].asInt(), 7200);
    EXPECT_EQ(json["token_type"].asString(), "Bearer");
    EXPECT_EQ(json["user"]["id"].asUInt64(), 12345);
    EXPECT_EQ(json["user"]["username"].asString(), "test_user");
    EXPECT_EQ(json["user"]["email"].asString(), "test@example.com");
}

TEST(RefreshTokenResponse, ToJsonCorrectFields) {
    RefreshTokenResponse response;
    response.access_token = "new_access_token_789";
    response.refresh_token = "new_refresh_token_012";
    response.expires_in = 7200;

    auto json = response.ToJson();

    EXPECT_EQ(json["access_token"].asString(), "new_access_token_789");
    EXPECT_EQ(json["refresh_token"].asString(), "new_refresh_token_012");
    EXPECT_EQ(json["expires_in"].asInt(), 7200);
}
