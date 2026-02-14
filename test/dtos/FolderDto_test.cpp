/**
 * @file FolderDto_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Folder DTO unit tests
 * @version 0.1
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "dtos/FolderDto.hpp"

#include <string>

#include <drogon/HttpRequest.h>
#include <drogon/utils/Utilities.h>
#include <gtest/gtest.h>
#include <json/json.h>

#include "utils/ErrorCode.hpp"

using disk::folder::CreateFolderRequest;
using disk::folder::CreateFolderResponse;

static auto CreateCreateFolderRequest(
    const std::string& name,
    uint64_t parent_id = 0
) -> drogon::HttpRequestPtr {
    Json::Value json;
    json["name"] = name;
    json["parent_id"] = static_cast<Json::UInt64>(parent_id);

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    return req;
}

static auto CreateCreateFolderRequestWithoutParent(
    const std::string& name
) -> drogon::HttpRequestPtr {
    Json::Value json;
    json["name"] = name;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    return req;
}

// ==================== CreateFolderRequest Tests ====================

TEST(CreateFolderRequest, CreateFolderRequestValidName) {
    auto req = CreateCreateFolderRequest("Documents");
    auto result = CreateFolderRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid name should pass validation";
    EXPECT_EQ(result->name, "Documents");
    EXPECT_EQ(result->parent_id, 0);
}

TEST(CreateFolderRequest, CreateFolderRequestValidNameWithParent) {
    auto req = CreateCreateFolderRequest("SubFolder", 123);
    auto result = CreateFolderRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid name with parent_id should pass validation";
    EXPECT_EQ(result->name, "SubFolder");
    EXPECT_EQ(result->parent_id, 123);
}

TEST(CreateFolderRequest, CreateFolderRequestDefaultParent) {
    auto req = CreateCreateFolderRequestWithoutParent("NewFolder");
    auto result = CreateFolderRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid name without parent_id should use default 0";
    EXPECT_EQ(result->name, "NewFolder");
    EXPECT_EQ(result->parent_id, 0);
}

// Rule 1: Length validation (1-255 chars) -> ValidationFailed

TEST(CreateFolderRequest, CreateFolderRequestNameTooLong) {
    std::string name(256, 'a');
    auto req = CreateCreateFolderRequest(name);
    auto result = CreateFolderRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Name with 256 chars should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(CreateFolderRequest, CreateFolderRequestNameValid255Chars) {
    std::string name(255, 'a');
    auto req = CreateCreateFolderRequest(name);
    auto result = CreateFolderRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Name with 255 chars should pass";
    EXPECT_EQ(result->name.length(), 255);
}

TEST(CreateFolderRequest, CreateFolderRequestNameEmpty) {
    auto req = CreateCreateFolderRequest("");
    auto result = CreateFolderRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Empty name should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

// Rule 2: Forbidden characters -> InvalidFilename

TEST(CreateFolderRequest, CreateFolderRequestForbiddenCharSlash) {
    auto req = CreateCreateFolderRequest("My/Folder");
    auto result = CreateFolderRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Name with / should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidFilename);
    }
}

TEST(CreateFolderRequest, CreateFolderRequestForbiddenCharBackslash) {
    auto req = CreateCreateFolderRequest("My\\Folder");
    auto result = CreateFolderRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Name with \\ should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidFilename);
    }
}

TEST(CreateFolderRequest, CreateFolderRequestForbiddenCharColon) {
    auto req = CreateCreateFolderRequest("Folder:Name");
    auto result = CreateFolderRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Name with : should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidFilename);
    }
}

TEST(CreateFolderRequest, CreateFolderRequestForbiddenCharAsterisk) {
    auto req = CreateCreateFolderRequest("Folder*Name");
    auto result = CreateFolderRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Name with * should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidFilename);
    }
}

TEST(CreateFolderRequest, CreateFolderRequestForbiddenCharQuestion) {
    auto req = CreateCreateFolderRequest("Folder?Name");
    auto result = CreateFolderRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Name with ? should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidFilename);
    }
}

TEST(CreateFolderRequest, CreateFolderRequestForbiddenCharQuote) {
    auto req = CreateCreateFolderRequest("Folder\"Name");
    auto result = CreateFolderRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Name with \" should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidFilename);
    }
}

TEST(CreateFolderRequest, CreateFolderRequestForbiddenCharLessThan) {
    auto req = CreateCreateFolderRequest("Folder<Name");
    auto result = CreateFolderRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Name with < should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidFilename);
    }
}

TEST(CreateFolderRequest, CreateFolderRequestForbiddenCharGreaterThan) {
    auto req = CreateCreateFolderRequest("Folder>Name");
    auto result = CreateFolderRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Name with > should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidFilename);
    }
}

TEST(CreateFolderRequest, CreateFolderRequestForbiddenCharPipe) {
    auto req = CreateCreateFolderRequest("Folder|Name");
    auto result = CreateFolderRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Name with | should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidFilename);
    }
}

// Rule 3: Reserved names (. and ..) -> InvalidFilename

TEST(CreateFolderRequest, CreateFolderRequestReservedNameDot) {
    auto req = CreateCreateFolderRequest(".");
    auto result = CreateFolderRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Name '.' should fail (reserved name)";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidFilename);
    }
}

TEST(CreateFolderRequest, CreateFolderRequestReservedNameDoubleDot) {
    auto req = CreateCreateFolderRequest("..");
    auto result = CreateFolderRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Name '..' should fail (reserved name)";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidFilename);
    }
}

// Rule 4: Hidden folders (starts with .) -> InvalidFilename

TEST(CreateFolderRequest, CreateFolderRequestHiddenFolder) {
    auto req = CreateCreateFolderRequest(".hidden");
    auto result = CreateFolderRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Name starting with . should fail (hidden folder)";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidFilename);
    }
}

// Rule 5: Charset (ASCII printable only) -> InvalidFilename

TEST(CreateFolderRequest, CreateFolderRequestNonAscii) {
    auto req = CreateCreateFolderRequest("Folder\xF0\x9F\x93\x81");
    auto result = CreateFolderRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Name with non-ASCII chars should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidFilename);
    }
}

TEST(CreateFolderRequest, CreateFolderRequestControlChar) {
    auto req = CreateCreateFolderRequest("Folder\x01Name");
    auto result = CreateFolderRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Name with control char should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidFilename);
    }
}

// Rule 6: Whitespace trimming

TEST(CreateFolderRequest, CreateFolderRequestWhitespaceTrim) {
    auto req = CreateCreateFolderRequest("  test  ");
    auto result = CreateFolderRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Name with leading/trailing spaces should pass after trim";
    EXPECT_EQ(result->name, "test") << "Spaces should be trimmed";
}

TEST(CreateFolderRequest, CreateFolderRequestWhitespaceOnly) {
    auto req = CreateCreateFolderRequest("   ");
    auto result = CreateFolderRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Whitespace-only name should fail after trim (empty)";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

// Additional edge cases

TEST(CreateFolderRequest, CreateFolderRequestMissingName) {
    Json::Value json;
    json["parent_id"] = 123;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto result = CreateFolderRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Missing name should fail";
}

TEST(CreateFolderRequest, CreateFolderRequestInvalidJSON) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody("{invalid json}");
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto result = CreateFolderRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Invalid JSON should fail";
}

TEST(CreateFolderRequest, CreateFolderRequestNameWrongType) {
    Json::Value json;
    json["name"] = 123;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto result = CreateFolderRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Name with wrong type should fail";
}

TEST(CreateFolderRequest, CreateFolderRequestParentIdWrongType) {
    Json::Value json;
    json["name"] = "TestFolder";
    json["parent_id"] = "not_a_number";

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto result = CreateFolderRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "parent_id with wrong type should fail";
}

// Valid names with special allowed characters

TEST(CreateFolderRequest, CreateFolderRequestValidNameWithUnderscore) {
    auto req = CreateCreateFolderRequest("Project_Alpha");
    auto result = CreateFolderRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Name with underscore should pass";
    EXPECT_EQ(result->name, "Project_Alpha");
}

TEST(CreateFolderRequest, CreateFolderRequestValidNameWithHyphen) {
    auto req = CreateCreateFolderRequest("my-folder");
    auto result = CreateFolderRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Name with hyphen should pass";
    EXPECT_EQ(result->name, "my-folder");
}

TEST(CreateFolderRequest, CreateFolderRequestValidNameWithNumbers) {
    auto req = CreateCreateFolderRequest("Folder2024");
    auto result = CreateFolderRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Name with numbers should pass";
    EXPECT_EQ(result->name, "Folder2024");
}

TEST(CreateFolderRequest, CreateFolderRequestValidNameWithSpace) {
    auto req = CreateCreateFolderRequest("My Folder");
    auto result = CreateFolderRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Name with internal space should pass";
    EXPECT_EQ(result->name, "My Folder");
}

// ==================== CreateFolderResponse Tests ====================

TEST(CreateFolderResponse, CreateFolderResponseToJsonCorrectFields) {
    CreateFolderResponse response;
    response.id = 10;
    response.name = "NewFolder";
    response.parent_id = 0;
    response.path = "/NewFolder";
    response.created_at = "2026-02-14T12:00:00Z";

    auto json = response.ToJson();

    EXPECT_EQ(json["id"].asUInt64(), 10);
    EXPECT_EQ(json["name"].asString(), "NewFolder");
    EXPECT_EQ(json["parent_id"].asUInt64(), 0);
    EXPECT_EQ(json["path"].asString(), "/NewFolder");
    EXPECT_EQ(json["created_at"].asString(), "2026-02-14T12:00:00Z");
}

TEST(CreateFolderResponse, CreateFolderResponseToJsonWithParent) {
    CreateFolderResponse response;
    response.id = 20;
    response.name = "SubFolder";
    response.parent_id = 10;
    response.path = "/Parent/SubFolder";
    response.created_at = "2026-02-14T12:30:00Z";

    auto json = response.ToJson();

    EXPECT_EQ(json["id"].asUInt64(), 20);
    EXPECT_EQ(json["name"].asString(), "SubFolder");
    EXPECT_EQ(json["parent_id"].asUInt64(), 10);
    EXPECT_EQ(json["path"].asString(), "/Parent/SubFolder");
    EXPECT_EQ(json["created_at"].asString(), "2026-02-14T12:30:00Z");
}
