/**
 * @file ShareDto_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Share DTO unit tests
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "dtos/ShareDto.hpp"

#include <string>

#include <drogon/HttpRequest.h>
#include <drogon/utils/Utilities.h>
#include <gtest/gtest.h>
#include <json/json.h>

#include "utils/ErrorCode.hpp"

using disk::share::AccessShareRequest;
using disk::share::AccessShareResponse;
using disk::share::BrowseBreadcrumb;
using disk::share::BrowseItem;
using disk::share::BrowseShareRequest;
using disk::share::BrowseShareResponse;
using disk::share::CancelShareError;
using disk::share::CancelShareRequest;
using disk::share::CancelShareResponse;
using disk::share::CancelShareResult;
using disk::share::CreateShareRequest;
using disk::share::CreateShareResponse;
using disk::share::DownloadShareRequest;
using disk::share::Pagination;
using disk::share::ShareDetailRequest;
using disk::share::ShareDetailResponse;
using disk::share::ShareFile;
using disk::share::ShareItem;
using disk::share::ShareListRequest;
using disk::share::ShareListResponse;
using disk::share::SharePermission;
using disk::share::SharePermissionToString;
using disk::share::ShareStatus;
using disk::share::ShareStatusToString;
using disk::share::StringToSharePermission;
using disk::share::UpdateShareRequest;
using disk::share::UpdateShareResponse;

// ==================== Helper Functions ====================

static auto CreateJsonRequest(const Json::Value& json) -> drogon::HttpRequestPtr {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    std::string body = Json::writeString(builder, json);

    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody(body);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    return req;
}

static auto CreateQueryRequest(const std::map<std::string, std::string>& params) -> drogon::HttpRequestPtr {
    auto req = drogon::HttpRequest::newHttpRequest();
    for (const auto& [key, value] : params) {
        req->setParameter(key, value);
    }
    return req;
}

// ==================== ShareStatus Tests ====================

TEST(ShareStatus, ToStringActive) {
    EXPECT_EQ(ShareStatusToString(ShareStatus::Active), "active");
}

TEST(ShareStatus, ToStringExpired) {
    EXPECT_EQ(ShareStatusToString(ShareStatus::Expired), "expired");
}

TEST(ShareStatus, ToStringCancelled) {
    EXPECT_EQ(ShareStatusToString(ShareStatus::Cancelled), "cancelled");
}

// ==================== SharePermission Tests ====================

TEST(SharePermission, ToStringView) {
    EXPECT_EQ(SharePermissionToString(SharePermission::View), "view");
}

TEST(SharePermission, ToStringDownload) {
    EXPECT_EQ(SharePermissionToString(SharePermission::Download), "download");
}

TEST(SharePermission, StringToView) {
    auto result = StringToSharePermission("view");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, SharePermission::View);
}

TEST(SharePermission, StringToDownload) {
    auto result = StringToSharePermission("download");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, SharePermission::Download);
}

TEST(SharePermission, StringToInvalid) {
    auto result = StringToSharePermission("invalid");
    EXPECT_FALSE(result.has_value());
}

TEST(SharePermission, StringToEmpty) {
    auto result = StringToSharePermission("");
    EXPECT_FALSE(result.has_value());
}

// ==================== Pagination Tests ====================

TEST(Pagination, ToJsonCorrectFields) {
    Pagination pagination;
    pagination.page = 2;
    pagination.page_size = 50;
    pagination.total = 123;
    pagination.total_pages = 3;

    auto json = pagination.ToJson();

    EXPECT_EQ(json["page"].asInt(), 2);
    EXPECT_EQ(json["page_size"].asInt(), 50);
    EXPECT_EQ(json["total"].asInt(), 123);
    EXPECT_EQ(json["total_pages"].asInt(), 3);
}

// ==================== ShareFile Tests ====================

TEST(ShareFile, ToJsonCorrectFields) {
    ShareFile file;
    file.id = 1;
    file.name = "document.pdf";
    file.type = "file";
    file.size = 102400;

    auto json = file.ToJson();

    EXPECT_EQ(json["id"].asUInt64(), 1);
    EXPECT_EQ(json["name"].asString(), "document.pdf");
    EXPECT_EQ(json["type"].asString(), "file");
    EXPECT_EQ(json["size"].asUInt64(), 102400);
}

TEST(ShareFile, ToJsonFolder) {
    ShareFile folder;
    folder.id = 2;
    folder.name = "Documents";
    folder.type = "folder";
    folder.size = 0;

    auto json = folder.ToJson();

    EXPECT_EQ(json["type"].asString(), "folder");
}

// ==================== CreateShareRequest Tests ====================

TEST(CreateShareRequest, ValidParametersMinimal) {
    Json::Value json;
    json["file_ids"].append(1);
    json["file_ids"].append(2);

    auto req = CreateJsonRequest(json);
    auto result = CreateShareRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid parameters should pass";
    EXPECT_EQ(result->file_ids.size(), 2);
    EXPECT_EQ(result->file_ids[0], 1);
    EXPECT_EQ(result->file_ids[1], 2);
    EXPECT_EQ(result->expire_days, 7);                        // 默认值
    EXPECT_EQ(result->permission, SharePermission::Download); // 默认值
    EXPECT_FALSE(result->password.has_value());
}

TEST(CreateShareRequest, ValidParametersAllFields) {
    Json::Value json;
    json["file_ids"].append(1);
    json["expire_days"] = 30;
    json["password"] = "abc123";
    json["permission"] = "view";

    auto req = CreateJsonRequest(json);
    auto result = CreateShareRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid parameters with all fields should pass";
    EXPECT_EQ(result->expire_days, 30);
    EXPECT_TRUE(result->password.has_value());
    EXPECT_EQ(*result->password, "abc123");
    EXPECT_EQ(result->permission, SharePermission::View);
}

TEST(CreateShareRequest, ValidExpireDaysZero) {
    Json::Value json;
    json["file_ids"].append(1);
    json["expire_days"] = 0; // 永久

    auto req = CreateJsonRequest(json);
    auto result = CreateShareRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "expire_days=0 should pass (permanent)";
    EXPECT_EQ(result->expire_days, 0);
}

TEST(CreateShareRequest, MissingFileIds) {
    Json::Value json;
    json["expire_days"] = 7;

    auto req = CreateJsonRequest(json);
    auto result = CreateShareRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Missing file_ids should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(CreateShareRequest, EmptyFileIdsArray) {
    Json::Value json;
    json["file_ids"] = Json::Value(Json::arrayValue);

    auto req = CreateJsonRequest(json);
    auto result = CreateShareRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Empty file_ids array should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(CreateShareRequest, FileIdsNotArray) {
    Json::Value json;
    json["file_ids"] = "not_an_array";

    auto req = CreateJsonRequest(json);
    auto result = CreateShareRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "file_ids not array should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(CreateShareRequest, FileIdZero) {
    Json::Value json;
    json["file_ids"].append(0); // 无效：必须为正数

    auto req = CreateJsonRequest(json);
    auto result = CreateShareRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "file_id=0 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(CreateShareRequest, NegativeExpireDays) {
    Json::Value json;
    json["file_ids"].append(1);
    json["expire_days"] = -1;

    auto req = CreateJsonRequest(json);
    auto result = CreateShareRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Negative expire_days should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(CreateShareRequest, PasswordTooShort) {
    Json::Value json;
    json["file_ids"].append(1);
    json["password"] = "abc"; // 3 chars, min is 4

    auto req = CreateJsonRequest(json);
    auto result = CreateShareRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Password too short should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(CreateShareRequest, PasswordTooLong) {
    Json::Value json;
    json["file_ids"].append(1);
    json["password"] = "abcdefghi"; // 9 chars, max is 8

    auto req = CreateJsonRequest(json);
    auto result = CreateShareRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Password too long should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(CreateShareRequest, PasswordMinLength) {
    Json::Value json;
    json["file_ids"].append(1);
    json["password"] = "abcd"; // 4 chars, min

    auto req = CreateJsonRequest(json);
    auto result = CreateShareRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Password with 4 chars should pass";
    EXPECT_EQ(*result->password, "abcd");
}

TEST(CreateShareRequest, PasswordMaxLength) {
    Json::Value json;
    json["file_ids"].append(1);
    json["password"] = "abcdefgh"; // 8 chars, max

    auto req = CreateJsonRequest(json);
    auto result = CreateShareRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Password with 8 chars should pass";
    EXPECT_EQ(*result->password, "abcdefgh");
}

TEST(CreateShareRequest, InvalidPermission) {
    Json::Value json;
    json["file_ids"].append(1);
    json["permission"] = "invalid";

    auto req = CreateJsonRequest(json);
    auto result = CreateShareRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Invalid permission should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(CreateShareRequest, InvalidJSON) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody("{invalid json}");
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

    auto result = CreateShareRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Invalid JSON should fail";
}

// ==================== CreateShareResponse Tests ====================

TEST(CreateShareResponse, ToJsonWithPassword) {
    CreateShareResponse response;
    response.share_id = "sh_abc123";
    response.share_link = "https://disk.example.com/s/abc123";
    response.password = "abc123";
    response.permission = "download";
    response.expires_at = "2026-02-20T10:00:00Z";
    response.created_at = "2026-02-13T10:00:00Z";

    auto json = response.ToJson();

    EXPECT_EQ(json["share_id"].asString(), "sh_abc123");
    EXPECT_EQ(json["share_link"].asString(), "https://disk.example.com/s/abc123");
    EXPECT_TRUE(json.isMember("password"));
    EXPECT_EQ(json["password"].asString(), "abc123");
    EXPECT_EQ(json["permission"].asString(), "download");
    EXPECT_EQ(json["expires_at"].asString(), "2026-02-20T10:00:00Z");
    EXPECT_EQ(json["created_at"].asString(), "2026-02-13T10:00:00Z");
}

TEST(CreateShareResponse, ToJsonWithoutPassword) {
    CreateShareResponse response;
    response.share_id = "sh_xyz789";
    response.share_link = "https://disk.example.com/s/xyz789";
    response.permission = "view";
    response.expires_at = "2026-02-20T10:00:00Z";
    response.created_at = "2026-02-13T10:00:00Z";

    auto json = response.ToJson();

    EXPECT_EQ(json["share_id"].asString(), "sh_xyz789");
    EXPECT_FALSE(json.isMember("password"));
}

// ==================== ShareListRequest Tests ====================

TEST(ShareListRequest, ValidDefaultParameters) {
    auto req = CreateQueryRequest({});
    auto result = ShareListRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Empty query should pass with defaults";
    EXPECT_EQ(result->status, "all");
    EXPECT_EQ(result->page, 1);
    EXPECT_EQ(result->page_size, 20);
}

TEST(ShareListRequest, ValidStatusActive) {
    auto req = CreateQueryRequest({
        { "status", "active" }
    });
    auto result = ShareListRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid status should pass";
    EXPECT_EQ(result->status, "active");
}

TEST(ShareListRequest, ValidStatusExpired) {
    auto req = CreateQueryRequest({
        { "status", "expired" }
    });
    auto result = ShareListRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid status should pass";
    EXPECT_EQ(result->status, "expired");
}

TEST(ShareListRequest, ValidStatusCancelled) {
    auto req = CreateQueryRequest({
        { "status", "cancelled" }
    });
    auto result = ShareListRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid status should pass";
    EXPECT_EQ(result->status, "cancelled");
}

TEST(ShareListRequest, InvalidStatus) {
    auto req = CreateQueryRequest({
        { "status", "invalid" }
    });
    auto result = ShareListRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Invalid status should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ShareListRequest, InvalidPageZero) {
    auto req = CreateQueryRequest({
        { "page", "0" }
    });
    auto result = ShareListRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "page=0 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ShareListRequest, InvalidPageNegative) {
    auto req = CreateQueryRequest({
        { "page", "-1" }
    });
    auto result = ShareListRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Negative page should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ShareListRequest, InvalidPageSizeZero) {
    auto req = CreateQueryRequest({
        { "page_size", "0" }
    });
    auto result = ShareListRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "page_size=0 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ShareListRequest, InvalidPageSizeTooLarge) {
    auto req = CreateQueryRequest({
        { "page_size", "101" }
    });
    auto result = ShareListRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "page_size>100 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ShareListRequest, ValidPageSizeMax) {
    auto req = CreateQueryRequest({
        { "page_size", "100" }
    });
    auto result = ShareListRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "page_size=100 should pass";
    EXPECT_EQ(result->page_size, 100);
}

// ==================== ShareItem Tests ====================

TEST(ShareItem, ToJsonCorrectFields) {
    ShareItem item;
    item.share_id = "sh_abc123";
    item.file_name = "document.pdf";
    item.file_count = 3;
    item.share_link = "https://disk.example.com/s/abc123";
    item.has_password = true;
    item.permission = "download";
    item.view_count = 10;
    item.download_count = 5;
    item.created_at = "2026-02-13T10:00:00Z";
    item.expires_at = "2026-02-20T10:00:00Z";
    item.status = "active";

    auto json = item.ToJson();

    EXPECT_EQ(json["share_id"].asString(), "sh_abc123");
    EXPECT_EQ(json["file_name"].asString(), "document.pdf");
    EXPECT_EQ(json["file_count"].asInt(), 3);
    EXPECT_TRUE(json["has_password"].asBool());
    EXPECT_EQ(json["permission"].asString(), "download");
    EXPECT_EQ(json["view_count"].asInt(), 10);
    EXPECT_EQ(json["download_count"].asInt(), 5);
    EXPECT_EQ(json["status"].asString(), "active");
}

// ==================== ShareListResponse Tests ====================

TEST(ShareListResponse, ToJsonCorrectFields) {
    ShareListResponse response;
    response.items.resize(2);
    response.items[0].share_id = "sh_abc123";
    response.items[0].file_name = "file1.pdf";
    response.items[1].share_id = "sh_def456";
    response.items[1].file_name = "file2.pdf";
    response.pagination.page = 1;
    response.pagination.page_size = 20;
    response.pagination.total = 2;
    response.pagination.total_pages = 1;

    auto json = response.ToJson();

    EXPECT_TRUE(json["items"].isArray());
    EXPECT_EQ(json["items"].size(), 2);
    EXPECT_EQ(json["items"][0]["share_id"].asString(), "sh_abc123");
    EXPECT_TRUE(json.isMember("pagination"));
}

// ==================== ShareDetailRequest Tests ====================

TEST(ShareDetailRequest, ValidShareId) {
    auto result = ShareDetailRequest::FromPath("sh_abc123");

    ASSERT_TRUE(result.has_value()) << "Valid share_id should pass";
    EXPECT_EQ(result->share_id, "sh_abc123");
}

TEST(ShareDetailRequest, EmptyShareId) {
    auto result = ShareDetailRequest::FromPath("");

    EXPECT_FALSE(result.has_value()) << "Empty share_id should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

// ==================== ShareDetailResponse Tests ====================

TEST(ShareDetailResponse, ToJsonCorrectFields) {
    ShareDetailResponse response;
    response.share_id = "sh_abc123";
    response.files.resize(1);
    response.files[0].id = 1;
    response.files[0].name = "document.pdf";
    response.files[0].type = "file";
    response.files[0].size = 102400;
    response.share_link = "https://disk.example.com/s/abc123";
    response.has_password = true;
    response.permission = "download";
    response.view_count = 10;
    response.download_count = 5;
    response.created_at = "2026-02-13T10:00:00Z";
    response.expires_at = "2026-02-20T10:00:00Z";
    response.status = "active";

    auto json = response.ToJson();

    EXPECT_EQ(json["share_id"].asString(), "sh_abc123");
    EXPECT_TRUE(json["files"].isArray());
    EXPECT_EQ(json["files"].size(), 1);
    EXPECT_TRUE(json["has_password"].asBool());
    EXPECT_EQ(json["status"].asString(), "active");
}

// ==================== UpdateShareRequest Tests ====================

TEST(UpdateShareRequest, ValidUpdateExpireDays) {
    Json::Value json;
    json["expire_days"] = 30;

    auto req = CreateJsonRequest(json);
    auto result = UpdateShareRequest::FromRequest(req, "sh_abc123");

    ASSERT_TRUE(result.has_value()) << "Valid update should pass";
    EXPECT_EQ(result->share_id, "sh_abc123");
    EXPECT_TRUE(result->expire_days.has_value());
    EXPECT_EQ(*result->expire_days, 30);
}

TEST(UpdateShareRequest, ValidUpdatePassword) {
    Json::Value json;
    json["password"] = "newpass";

    auto req = CreateJsonRequest(json);
    auto result = UpdateShareRequest::FromRequest(req, "sh_abc123");

    ASSERT_TRUE(result.has_value()) << "Valid password update should pass";
    EXPECT_TRUE(result->password.has_value());
    EXPECT_EQ(*result->password, "newpass");
}

TEST(UpdateShareRequest, ValidRemovePassword) {
    Json::Value json;
    json["password"] = ""; // 空字符串移除密码

    auto req = CreateJsonRequest(json);
    auto result = UpdateShareRequest::FromRequest(req, "sh_abc123");

    ASSERT_TRUE(result.has_value()) << "Empty password should pass (remove password)";
    EXPECT_TRUE(result->password.has_value());
    EXPECT_EQ(*result->password, "");
}

TEST(UpdateShareRequest, ValidUpdatePermission) {
    Json::Value json;
    json["permission"] = "view";

    auto req = CreateJsonRequest(json);
    auto result = UpdateShareRequest::FromRequest(req, "sh_abc123");

    ASSERT_TRUE(result.has_value()) << "Valid permission update should pass";
    EXPECT_TRUE(result->permission.has_value());
    EXPECT_EQ(*result->permission, SharePermission::View);
}

TEST(UpdateShareRequest, EmptyShareId) {
    Json::Value json;
    json["expire_days"] = 30;

    auto req = CreateJsonRequest(json);
    auto result = UpdateShareRequest::FromRequest(req, "");

    EXPECT_FALSE(result.has_value()) << "Empty share_id should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(UpdateShareRequest, InvalidExpireDays) {
    Json::Value json;
    json["expire_days"] = -1;

    auto req = CreateJsonRequest(json);
    auto result = UpdateShareRequest::FromRequest(req, "sh_abc123");

    EXPECT_FALSE(result.has_value()) << "Negative expire_days should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(UpdateShareRequest, InvalidPasswordLength) {
    Json::Value json;
    json["password"] = "abc"; // 过短

    auto req = CreateJsonRequest(json);
    auto result = UpdateShareRequest::FromRequest(req, "sh_abc123");

    EXPECT_FALSE(result.has_value()) << "Password too short should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(UpdateShareRequest, InvalidPermission) {
    Json::Value json;
    json["permission"] = "invalid";

    auto req = CreateJsonRequest(json);
    auto result = UpdateShareRequest::FromRequest(req, "sh_abc123");

    EXPECT_FALSE(result.has_value()) << "Invalid permission should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

// ==================== UpdateShareResponse Tests ====================

TEST(UpdateShareResponse, ToJsonCorrectFields) {
    UpdateShareResponse response;
    response.share_id = "sh_abc123";
    response.expires_at = "2026-03-13T10:00:00Z";
    response.has_password = false;
    response.permission = "view";
    response.updated_at = "2026-02-13T11:00:00Z";

    auto json = response.ToJson();

    EXPECT_EQ(json["share_id"].asString(), "sh_abc123");
    EXPECT_EQ(json["expires_at"].asString(), "2026-03-13T10:00:00Z");
    EXPECT_FALSE(json["has_password"].asBool());
    EXPECT_EQ(json["permission"].asString(), "view");
    EXPECT_EQ(json["updated_at"].asString(), "2026-02-13T11:00:00Z");
}

// ==================== CancelShareRequest Tests ====================

TEST(CancelShareRequest, ValidParameters) {
    Json::Value json;
    json["share_ids"].append("sh_abc123");
    json["share_ids"].append("sh_def456");

    auto req = CreateJsonRequest(json);
    auto result = CancelShareRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid parameters should pass";
    EXPECT_EQ(result->share_ids.size(), 2);
    EXPECT_EQ(result->share_ids[0], "sh_abc123");
    EXPECT_EQ(result->share_ids[1], "sh_def456");
}

TEST(CancelShareRequest, MissingShareIds) {
    Json::Value json;

    auto req = CreateJsonRequest(json);
    auto result = CancelShareRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Missing share_ids should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(CancelShareRequest, EmptyShareIdsArray) {
    Json::Value json;
    json["share_ids"] = Json::Value(Json::arrayValue);

    auto req = CreateJsonRequest(json);
    auto result = CancelShareRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Empty share_ids array should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(CancelShareRequest, ShareIdsNotArray) {
    Json::Value json;
    json["share_ids"] = "not_an_array";

    auto req = CreateJsonRequest(json);
    auto result = CancelShareRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "share_ids not array should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(CancelShareRequest, EmptyShareIdInArray) {
    Json::Value json;
    json["share_ids"].append("");

    auto req = CreateJsonRequest(json);
    auto result = CancelShareRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Empty share_id in array should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

// ==================== CancelShareResponse Tests ====================

TEST(CancelShareResponse, ToJsonAllSuccess) {
    CancelShareResponse response;
    response.summary.total = 2;
    response.summary.succeeded = 2;
    response.summary.failed = 0;

    CancelShareResult result1;
    result1.share_id = "sh_abc123";
    result1.status = "success";

    CancelShareResult result2;
    result2.share_id = "sh_def456";
    result2.status = "success";

    response.results.push_back(result1);
    response.results.push_back(result2);

    auto json = response.ToJson();

    EXPECT_EQ(json["summary"]["total"].asInt(), 2);
    EXPECT_EQ(json["summary"]["succeeded"].asInt(), 2);
    EXPECT_EQ(json["summary"]["failed"].asInt(), 0);
    EXPECT_EQ(json["results"].size(), 2);
    EXPECT_EQ(json["results"][0]["status"].asString(), "success");
}

TEST(CancelShareResponse, ToJsonPartialSuccess) {
    CancelShareResponse response;
    response.summary.total = 3;
    response.summary.succeeded = 1;
    response.summary.failed = 2;

    CancelShareResult result1;
    result1.share_id = "sh_abc123";
    result1.status = "success";

    CancelShareResult result2;
    result2.share_id = "sh_invalid";
    result2.status = "failed";
    result2.error = CancelShareError{ .code = 60001, .message = "分享不存在", .reason = "分享不存在或不属于当前用户" };

    CancelShareResult result3;
    result3.share_id = "sh_expired";
    result3.status = "failed";
    result3.error = CancelShareError{ .code = 60002, .message = "分享已过期", .reason = "分享已超过有效期，无法取消" };

    response.results.push_back(result1);
    response.results.push_back(result2);
    response.results.push_back(result3);

    auto json = response.ToJson();

    EXPECT_EQ(json["summary"]["succeeded"].asInt(), 1);
    EXPECT_EQ(json["summary"]["failed"].asInt(), 2);
    EXPECT_EQ(json["results"][1]["status"].asString(), "failed");
    EXPECT_TRUE(json["results"][1].isMember("error"));
    EXPECT_EQ(json["results"][1]["error"]["code"].asInt(), 60001);
    EXPECT_FALSE(json["results"][0].isMember("error"));
}

// ==================== AccessShareRequest Tests ====================

TEST(AccessShareRequest, ValidWithoutPassword) {
    Json::Value json;

    auto req = CreateJsonRequest(json);
    auto result = AccessShareRequest::FromRequest(req, "sh_abc123");

    ASSERT_TRUE(result.has_value()) << "Valid without password should pass";
    EXPECT_EQ(result->share_id, "sh_abc123");
    EXPECT_FALSE(result->password.has_value());
}

TEST(AccessShareRequest, ValidWithPassword) {
    Json::Value json;
    json["password"] = "abc123";

    auto req = CreateJsonRequest(json);
    auto result = AccessShareRequest::FromRequest(req, "sh_abc123");

    ASSERT_TRUE(result.has_value()) << "Valid with password should pass";
    EXPECT_TRUE(result->password.has_value());
    EXPECT_EQ(*result->password, "abc123");
}

TEST(AccessShareRequest, EmptyShareId) {
    Json::Value json;
    json["password"] = "abc123";

    auto req = CreateJsonRequest(json);
    auto result = AccessShareRequest::FromRequest(req, "");

    EXPECT_FALSE(result.has_value()) << "Empty share_id should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(AccessShareRequest, PasswordWrongType) {
    Json::Value json;
    json["password"] = 123;

    auto req = CreateJsonRequest(json);
    auto result = AccessShareRequest::FromRequest(req, "sh_abc123");

    EXPECT_FALSE(result.has_value()) << "Password wrong type should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

// ==================== AccessShareResponse Tests ====================

TEST(AccessShareResponse, ToJsonCorrectFields) {
    AccessShareResponse response;
    response.share_token = "st_xyz789...";
    response.expires_in = 3600;
    response.permission = "download";
    response.files.resize(1);
    response.files[0].id = 1;
    response.files[0].name = "document.pdf";
    response.files[0].type = "file";
    response.files[0].size = 102400;

    auto json = response.ToJson();

    EXPECT_EQ(json["share_token"].asString(), "st_xyz789...");
    EXPECT_EQ(json["expires_in"].asInt(), 3600);
    EXPECT_EQ(json["permission"].asString(), "download");
    EXPECT_TRUE(json["files"].isArray());
    EXPECT_EQ(json["files"].size(), 1);
}

// ==================== BrowseShareRequest Tests ====================

TEST(BrowseShareRequest, ValidWithoutFolderId) {
    auto req = CreateQueryRequest({});
    auto result = BrowseShareRequest::FromRequest(req, "sh_abc123");

    ASSERT_TRUE(result.has_value()) << "Valid without folder_id should pass";
    EXPECT_EQ(result->share_id, "sh_abc123");
    EXPECT_FALSE(result->folder_id.has_value());
}

TEST(BrowseShareRequest, ValidWithFolderId) {
    auto req = CreateQueryRequest({
        { "folder_id", "42" }
    });
    auto result = BrowseShareRequest::FromRequest(req, "sh_abc123");

    ASSERT_TRUE(result.has_value()) << "Valid with folder_id should pass";
    EXPECT_EQ(result->share_id, "sh_abc123");
    EXPECT_TRUE(result->folder_id.has_value());
    EXPECT_EQ(*result->folder_id, 42);
}

TEST(BrowseShareRequest, EmptyShareId) {
    auto req = CreateQueryRequest({});
    auto result = BrowseShareRequest::FromRequest(req, "");

    EXPECT_FALSE(result.has_value()) << "Empty share_id should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(BrowseShareRequest, InvalidFolderId) {
    auto req = CreateQueryRequest({
        { "folder_id", "invalid" }
    });
    auto result = BrowseShareRequest::FromRequest(req, "sh_abc123");

    EXPECT_FALSE(result.has_value()) << "Invalid folder_id should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

// ==================== BrowseShareResponse Tests ====================

TEST(BrowseShareResponse, ToJsonCorrectFields) {
    BrowseShareResponse response;

    BrowseItem item;
    item.id = 1;
    item.name = "子文件.txt";
    item.type = "file";
    item.size = 1024;
    response.items.push_back(item);

    BrowseBreadcrumb breadcrumb;
    breadcrumb.id = 0;
    breadcrumb.name = "分享根目录";
    response.breadcrumb.push_back(breadcrumb);

    auto json = response.ToJson();

    EXPECT_TRUE(json["items"].isArray());
    EXPECT_EQ(json["items"].size(), 1);
    EXPECT_EQ(json["items"][0]["name"].asString(), "子文件.txt");

    EXPECT_TRUE(json["breadcrumb"].isArray());
    EXPECT_EQ(json["breadcrumb"].size(), 1);
    EXPECT_EQ(json["breadcrumb"][0]["name"].asString(), "分享根目录");
}

// ==================== DownloadShareRequest Tests ====================

TEST(DownloadShareRequest, ValidParameters) {
    auto result = DownloadShareRequest::FromPath("sh_abc123", "42");

    ASSERT_TRUE(result.has_value()) << "Valid parameters should pass";
    EXPECT_EQ(result->share_id, "sh_abc123");
    EXPECT_EQ(result->file_id, 42);
}

TEST(DownloadShareRequest, EmptyShareId) {
    auto result = DownloadShareRequest::FromPath("", "42");

    EXPECT_FALSE(result.has_value()) << "Empty share_id should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(DownloadShareRequest, EmptyFileId) {
    auto result = DownloadShareRequest::FromPath("sh_abc123", "");

    EXPECT_FALSE(result.has_value()) << "Empty file_id should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(DownloadShareRequest, FileIdZero) {
    auto result = DownloadShareRequest::FromPath("sh_abc123", "0");

    EXPECT_FALSE(result.has_value()) << "file_id=0 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(DownloadShareRequest, InvalidFileId) {
    auto result = DownloadShareRequest::FromPath("sh_abc123", "invalid");

    EXPECT_FALSE(result.has_value()) << "Invalid file_id should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}

TEST(DownloadShareRequest, FileIdNegative) {
    auto result = DownloadShareRequest::FromPath("sh_abc123", "-1");

    EXPECT_FALSE(result.has_value()) << "Negative file_id should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
    }
}
