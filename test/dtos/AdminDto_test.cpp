/**
 * @file AdminDto_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Admin DTO unit tests
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "dtos/AdminDto.hpp"

#include <map>
#include <set>
#include <string>

#include <drogon/HttpRequest.h>
#include <drogon/utils/Utilities.h>
#include <gtest/gtest.h>
#include <json/json.h>

#include "utils/ErrorCode.hpp"

using namespace disk::admin;
using disk::error::Code;

// ==================== Helper Functions ====================

namespace {
    auto CreateJsonRequest(Json::Value&& json) -> drogon::HttpRequestPtr {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        std::string body = Json::writeString(builder, json);
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setBody(body);
        req->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        return req;
    }

    auto CreateJsonRequest(const Json::Value& json) -> drogon::HttpRequestPtr {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        std::string body = Json::writeString(builder, json);
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setBody(body);
        req->setContentTypeCode(drogon::CT_APPLICATION_JSON);
        return req;
    }

    auto CreateQueryRequest(std::map<std::string, std::string> params) -> drogon::HttpRequestPtr {
        auto req = drogon::HttpRequest::newHttpRequest();
        for (auto& [k, v] : params)
            req->setParameter(k, v);
        return req;
    }
} // namespace

// ==================== ListUsersRequest Tests ====================

TEST(ListUsersRequest, ValidParameters) {
    auto req = CreateQueryRequest({{"page", "1"}, {"page_size", "20"}});
    auto result = ListUsersRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid parameters should pass";
    EXPECT_EQ(result->page, 1);
    EXPECT_EQ(result->page_size, 20);
}

TEST(ListUsersRequest, DefaultPagination) {
    auto req = CreateQueryRequest({});
    auto result = ListUsersRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Empty query should use defaults";
    EXPECT_EQ(result->page, 1);
    EXPECT_EQ(result->page_size, 20);
    EXPECT_FALSE(result->username.has_value());
    EXPECT_FALSE(result->email.has_value());
    EXPECT_FALSE(result->status.has_value());
    EXPECT_FALSE(result->role.has_value());
}

TEST(ListUsersRequest, WithAllFilters) {
    auto req = CreateQueryRequest({
        {"page", "2"},
        {"page_size", "50"},
        {"username", "test"},
        {"email", "t@e.com"},
        {"status", "1"},
        {"role", "0"},
    });
    auto result = ListUsersRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "All filters should pass";
    EXPECT_EQ(result->page, 2);
    EXPECT_EQ(result->page_size, 50);
    ASSERT_TRUE(result->username.has_value());
    EXPECT_EQ(*result->username, "test");
    ASSERT_TRUE(result->email.has_value());
    EXPECT_EQ(*result->email, "t@e.com");
    ASSERT_TRUE(result->status.has_value());
    EXPECT_EQ(*result->status, 1);
    ASSERT_TRUE(result->role.has_value());
    EXPECT_EQ(*result->role, 0);
}

TEST(ListUsersRequest, InvalidPage_Zero) {
    auto req = CreateQueryRequest({{"page", "0"}});
    auto result = ListUsersRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "page=0 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ListUsersRequest, InvalidPage_Negative) {
    auto req = CreateQueryRequest({{"page", "-1"}});
    auto result = ListUsersRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "page=-1 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ListUsersRequest, InvalidPage_NonNumeric) {
    auto req = CreateQueryRequest({{"page", "abc"}});
    auto result = ListUsersRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "page=abc should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ListUsersRequest, InvalidPageSize_Zero) {
    auto req = CreateQueryRequest({{"page_size", "0"}});
    auto result = ListUsersRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "page_size=0 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ListUsersRequest, InvalidPageSize_Negative) {
    auto req = CreateQueryRequest({{"page_size", "-5"}});
    auto result = ListUsersRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "page_size=-5 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ListUsersRequest, InvalidPageSize_TooLarge) {
    auto req = CreateQueryRequest({{"page_size", "101"}});
    auto result = ListUsersRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "page_size=101 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ListUsersRequest, InvalidPageSize_NonNumeric) {
    auto req = CreateQueryRequest({{"page_size", "abc"}});
    auto result = ListUsersRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "page_size=abc should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ListUsersRequest, InvalidStatus_OutOfRange) {
    auto req = CreateQueryRequest({{"status", "3"}});
    auto result = ListUsersRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "status=3 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::AdminInvalidStatus);
    }
}

TEST(ListUsersRequest, InvalidStatus_Negative) {
    auto req = CreateQueryRequest({{"status", "-1"}});
    auto result = ListUsersRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "status=-1 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::AdminInvalidStatus);
    }
}

TEST(ListUsersRequest, InvalidStatus_NonNumeric) {
    auto req = CreateQueryRequest({{"status", "abc"}});
    auto result = ListUsersRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "status=abc should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ListUsersRequest, InvalidRole_OutOfRange) {
    auto req = CreateQueryRequest({{"role", "2"}});
    auto result = ListUsersRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "role=2 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::AdminInvalidRole);
    }
}

TEST(ListUsersRequest, InvalidRole_Negative) {
    auto req = CreateQueryRequest({{"role", "-1"}});
    auto result = ListUsersRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "role=-1 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::AdminInvalidRole);
    }
}

TEST(ListUsersRequest, InvalidRole_NonNumeric) {
    auto req = CreateQueryRequest({{"role", "abc"}});
    auto result = ListUsersRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "role=abc should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ListUsersRequest, Boundary_PageSize_100) {
    auto req = CreateQueryRequest({{"page_size", "100"}});
    auto result = ListUsersRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "page_size=100 should pass (max allowed)";
    EXPECT_EQ(result->page_size, 100);
}

TEST(ListUsersRequest, Boundary_PageSize_1) {
    auto req = CreateQueryRequest({{"page_size", "1"}});
    auto result = ListUsersRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "page_size=1 should pass (min allowed)";
    EXPECT_EQ(result->page_size, 1);
}

TEST(ListUsersRequest, ValidStatus_BoundaryValues) {
    // status 0 (disabled)
    {
        auto req = CreateQueryRequest({{"status", "0"}});
        auto result = ListUsersRequest::FromRequest(req);
        ASSERT_TRUE(result.has_value());
        ASSERT_TRUE(result->status.has_value());
        EXPECT_EQ(*result->status, 0);
    }
    // status 1 (active)
    {
        auto req = CreateQueryRequest({{"status", "1"}});
        auto result = ListUsersRequest::FromRequest(req);
        ASSERT_TRUE(result.has_value());
        ASSERT_TRUE(result->status.has_value());
        EXPECT_EQ(*result->status, 1);
    }
    // status 2 (locked)
    {
        auto req = CreateQueryRequest({{"status", "2"}});
        auto result = ListUsersRequest::FromRequest(req);
        ASSERT_TRUE(result.has_value());
        ASSERT_TRUE(result->status.has_value());
        EXPECT_EQ(*result->status, 2);
    }
}

TEST(ListUsersRequest, ValidRole_BoundaryValues) {
    // role 0 (user)
    {
        auto req = CreateQueryRequest({{"role", "0"}});
        auto result = ListUsersRequest::FromRequest(req);
        ASSERT_TRUE(result.has_value());
        ASSERT_TRUE(result->role.has_value());
        EXPECT_EQ(*result->role, 0);
    }
    // role 1 (admin)
    {
        auto req = CreateQueryRequest({{"role", "1"}});
        auto result = ListUsersRequest::FromRequest(req);
        ASSERT_TRUE(result.has_value());
        ASSERT_TRUE(result->role.has_value());
        EXPECT_EQ(*result->role, 1);
    }
}

TEST(ListUsersRequest, PartialParameters_UsernameOnly) {
    auto req = CreateQueryRequest({{"username", "alice"}});
    auto result = ListUsersRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->username.has_value());
    EXPECT_EQ(*result->username, "alice");
    EXPECT_EQ(result->page, 1);
    EXPECT_EQ(result->page_size, 20);
}

TEST(ListUsersRequest, PageWithTrailingChars) {
    auto req = CreateQueryRequest({{"page", "1abc"}});
    auto result = ListUsersRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "page with trailing chars should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

// ==================== ChangeStatusRequest Tests ====================

TEST(ChangeStatusRequest, ValidStatus_Disabled) {
    Json::Value json;
    json["status"] = 0;
    auto req = CreateJsonRequest(json);
    auto result = ChangeStatusRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "status=0 (disabled) should pass";
    EXPECT_EQ(result->status, 0);
}

TEST(ChangeStatusRequest, ValidStatus_Active) {
    Json::Value json;
    json["status"] = 1;
    auto req = CreateJsonRequest(json);
    auto result = ChangeStatusRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "status=1 (active) should pass";
    EXPECT_EQ(result->status, 1);
}

TEST(ChangeStatusRequest, ValidStatus_Locked) {
    Json::Value json;
    json["status"] = 2;
    auto req = CreateJsonRequest(json);
    auto result = ChangeStatusRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "status=2 (locked) should pass";
    EXPECT_EQ(result->status, 2);
}

TEST(ChangeStatusRequest, MissingField) {
    Json::Value json;
    json["other"] = "value";
    auto req = CreateJsonRequest(json);
    auto result = ChangeStatusRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Missing status field should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ChangeStatusRequest, WrongType_String) {
    Json::Value json;
    json["status"] = "abc";
    auto req = CreateJsonRequest(json);
    auto result = ChangeStatusRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "status as string should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ChangeStatusRequest, WrongType_Float) {
    Json::Value json;
    json["status"] = 1.5;
    auto req = CreateJsonRequest(json);
    auto result = ChangeStatusRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "status as float should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ChangeStatusRequest, InvalidStatus_OutOfRange) {
    Json::Value json;
    json["status"] = 3;
    auto req = CreateJsonRequest(json);
    auto result = ChangeStatusRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "status=3 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::AdminInvalidStatus);
    }
}

TEST(ChangeStatusRequest, InvalidStatus_Negative) {
    Json::Value json;
    json["status"] = -1;
    auto req = CreateJsonRequest(json);
    auto result = ChangeStatusRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "status=-1 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::AdminInvalidStatus);
    }
}

TEST(ChangeStatusRequest, EmptyBody) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody("");
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    auto result = ChangeStatusRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Empty body should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ChangeStatusRequest, InvalidJSON) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody("{invalid json}");
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    auto result = ChangeStatusRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Invalid JSON should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ChangeStatusRequest, WrongType_Boolean) {
    Json::Value json;
    json["status"] = true;
    auto req = CreateJsonRequest(json);
    auto result = ChangeStatusRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "status as boolean should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

// ==================== ChangeRoleRequest Tests ====================

TEST(ChangeRoleRequest, ValidRole_User) {
    Json::Value json;
    json["role"] = 0;
    auto req = CreateJsonRequest(json);
    auto result = ChangeRoleRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "role=0 (user) should pass";
    EXPECT_EQ(result->role, 0);
}

TEST(ChangeRoleRequest, ValidRole_Admin) {
    Json::Value json;
    json["role"] = 1;
    auto req = CreateJsonRequest(json);
    auto result = ChangeRoleRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "role=1 (admin) should pass";
    EXPECT_EQ(result->role, 1);
}

TEST(ChangeRoleRequest, MissingField) {
    Json::Value json;
    json["other"] = "value";
    auto req = CreateJsonRequest(json);
    auto result = ChangeRoleRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Missing role field should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ChangeRoleRequest, WrongType_String) {
    Json::Value json;
    json["role"] = "admin";
    auto req = CreateJsonRequest(json);
    auto result = ChangeRoleRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "role as string should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ChangeRoleRequest, InvalidRole_OutOfRange) {
    Json::Value json;
    json["role"] = 2;
    auto req = CreateJsonRequest(json);
    auto result = ChangeRoleRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "role=2 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::AdminInvalidRole);
    }
}

TEST(ChangeRoleRequest, InvalidRole_Negative) {
    Json::Value json;
    json["role"] = -1;
    auto req = CreateJsonRequest(json);
    auto result = ChangeRoleRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "role=-1 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::AdminInvalidRole);
    }
}

TEST(ChangeRoleRequest, EmptyBody) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody("");
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    auto result = ChangeRoleRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Empty body should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ChangeRoleRequest, InvalidJSON) {
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setBody("not json");
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    auto result = ChangeRoleRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "Invalid JSON should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

// ==================== ListSharesRequest Tests ====================

TEST(ListSharesRequest, ValidParameters) {
    auto req = CreateQueryRequest({{"page", "1"}, {"page_size", "20"}});
    auto result = ListSharesRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Valid parameters should pass";
    EXPECT_EQ(result->page, 1);
    EXPECT_EQ(result->page_size, 20);
}

TEST(ListSharesRequest, DefaultPagination) {
    auto req = CreateQueryRequest({});
    auto result = ListSharesRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Empty query should use defaults";
    EXPECT_EQ(result->page, 1);
    EXPECT_EQ(result->page_size, 20);
    EXPECT_FALSE(result->status.has_value());
    EXPECT_FALSE(result->user_id.has_value());
}

TEST(ListSharesRequest, WithFilters) {
    auto req = CreateQueryRequest({
        {"status", "1"},
        {"user_id", "42"},
    });
    auto result = ListSharesRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Filters should pass";
    ASSERT_TRUE(result->status.has_value());
    EXPECT_EQ(*result->status, 1);
    ASSERT_TRUE(result->user_id.has_value());
    EXPECT_EQ(*result->user_id, 42);
}

TEST(ListSharesRequest, InvalidStatus_OutOfRange) {
    auto req = CreateQueryRequest({{"status", "3"}});
    auto result = ListSharesRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "status=3 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::AdminInvalidStatus);
    }
}

TEST(ListSharesRequest, InvalidStatus_Negative) {
    auto req = CreateQueryRequest({{"status", "-1"}});
    auto result = ListSharesRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "status=-1 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::AdminInvalidStatus);
    }
}

TEST(ListSharesRequest, InvalidStatus_NonNumeric) {
    auto req = CreateQueryRequest({{"status", "abc"}});
    auto result = ListSharesRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "status=abc should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ListSharesRequest, InvalidUserId_NonNumeric) {
    auto req = CreateQueryRequest({{"user_id", "abc"}});
    auto result = ListSharesRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "user_id=abc should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ListSharesRequest, InvalidPage_Zero) {
    auto req = CreateQueryRequest({{"page", "0"}});
    auto result = ListSharesRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "page=0 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ListSharesRequest, InvalidPageSize_TooLarge) {
    auto req = CreateQueryRequest({{"page_size", "101"}});
    auto result = ListSharesRequest::FromRequest(req);

    EXPECT_FALSE(result.has_value()) << "page_size=101 should fail";
    if (!result.has_value()) {
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST(ListSharesRequest, Boundary_PageSize_100) {
    auto req = CreateQueryRequest({{"page_size", "100"}});
    auto result = ListSharesRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "page_size=100 should pass";
    EXPECT_EQ(result->page_size, 100);
}

TEST(ListSharesRequest, ValidStatus_AllValues) {
    for (int s = 0; s <= 2; ++s) {
        auto req = CreateQueryRequest({{"status", std::to_string(s)}});
        auto result = ListSharesRequest::FromRequest(req);
        ASSERT_TRUE(result.has_value()) << "status=" << s << " should pass";
        ASSERT_TRUE(result->status.has_value());
        EXPECT_EQ(*result->status, s);
    }
}

TEST(ListSharesRequest, UserId_LargeValue) {
    auto req = CreateQueryRequest({{"user_id", "18446744073709551615"}});
    auto result = ListSharesRequest::FromRequest(req);

    ASSERT_TRUE(result.has_value()) << "Large user_id should pass";
    ASSERT_TRUE(result->user_id.has_value());
    EXPECT_EQ(*result->user_id, 18446744073709551615ULL);
}

// ==================== PaginationInfo Tests ====================

TEST(PaginationInfo, BasicFields) {
    PaginationInfo pagination;
    pagination.page = 1;
    pagination.page_size = 20;
    pagination.total = 100;
    pagination.total_pages = 5;

    auto json = pagination.ToJson();

    EXPECT_EQ(json["page"].asInt(), 1);
    EXPECT_EQ(json["page_size"].asInt(), 20);
    EXPECT_EQ(json["total"].asInt(), 100);
    EXPECT_EQ(json["total_pages"].asInt(), 5);
}

TEST(PaginationInfo, ZeroTotal) {
    PaginationInfo pagination;
    pagination.page = 1;
    pagination.page_size = 20;
    pagination.total = 0;
    pagination.total_pages = 0;

    auto json = pagination.ToJson();

    EXPECT_EQ(json["total"].asInt(), 0);
    EXPECT_EQ(json["total_pages"].asInt(), 0);
}

TEST(PaginationInfo, PartialPage) {
    PaginationInfo pagination;
    pagination.page = 1;
    pagination.page_size = 20;
    pagination.total = 21;
    pagination.total_pages = 2;

    auto json = pagination.ToJson();

    EXPECT_EQ(json["total"].asInt(), 21);
    EXPECT_EQ(json["total_pages"].asInt(), 2);
}

TEST(PaginationInfo, ExactPage) {
    PaginationInfo pagination;
    pagination.page = 1;
    pagination.page_size = 20;
    pagination.total = 20;
    pagination.total_pages = 1;

    auto json = pagination.ToJson();

    EXPECT_EQ(json["total"].asInt(), 20);
    EXPECT_EQ(json["total_pages"].asInt(), 1);
}

TEST(PaginationInfo, SingleItem) {
    PaginationInfo pagination;
    pagination.page = 1;
    pagination.page_size = 20;
    pagination.total = 1;
    pagination.total_pages = 1;

    auto json = pagination.ToJson();

    EXPECT_EQ(json["total"].asInt(), 1);
    EXPECT_EQ(json["total_pages"].asInt(), 1);
}

TEST(PaginationInfo, DefaultValues) {
    PaginationInfo pagination;

    auto json = pagination.ToJson();

    EXPECT_EQ(json["page"].asInt(), 1);
    EXPECT_EQ(json["page_size"].asInt(), 20);
    EXPECT_EQ(json["total"].asInt(), 0);
    EXPECT_EQ(json["total_pages"].asInt(), 0);
}

// ==================== UserDetailResponse Tests ====================

TEST(UserDetailResponse, AllFields) {
    UserDetailResponse response;
    response.id = 42;
    response.username = "alice";
    response.email = "alice@example.com";
    response.nickname = "Alice";
    response.avatar = "https://example.com/avatar.png";
    response.role = 1;
    response.status = 1;
    response.storage_quota = 10737418240;
    response.storage_used = 5368709120;
    response.storage_reserved = 0;
    response.created_at = "2026-01-01T00:00:00Z";
    response.last_login_at = "2026-03-15T12:00:00Z";

    auto json = response.ToJson();

    EXPECT_EQ(json["id"].asUInt64(), 42);
    EXPECT_EQ(json["username"].asString(), "alice");
    EXPECT_EQ(json["email"].asString(), "alice@example.com");
    EXPECT_EQ(json["nickname"].asString(), "Alice");
    EXPECT_EQ(json["avatar"].asString(), "https://example.com/avatar.png");
    EXPECT_EQ(json["role"].asInt(), 1);
    EXPECT_EQ(json["status"].asInt(), 1);
    EXPECT_EQ(json["storage_quota"].asUInt64(), 10737418240ULL);
    EXPECT_EQ(json["storage_used"].asUInt64(), 5368709120ULL);
    EXPECT_EQ(json["storage_reserved"].asUInt64(), 0ULL);
    EXPECT_EQ(json["created_at"].asString(), "2026-01-01T00:00:00Z");
    EXPECT_EQ(json["last_login_at"].asString(), "2026-03-15T12:00:00Z");
}

TEST(UserDetailResponse, NullableFields) {
    UserDetailResponse response;
    response.id = 1;
    response.username = "bob";
    response.email = "bob@test.com";
    response.nickname = "";
    response.avatar = "";
    response.last_login_at = "";

    auto json = response.ToJson();

    EXPECT_EQ(json["nickname"].asString(), "");
    EXPECT_EQ(json["avatar"].asString(), "");
    EXPECT_EQ(json["last_login_at"].asString(), "");
}

TEST(UserDetailResponse, ZeroStorage) {
    UserDetailResponse response;
    response.id = 1;
    response.username = "newuser";
    response.email = "new@test.com";
    response.storage_used = 0;
    response.storage_quota = 0;
    response.storage_reserved = 0;

    auto json = response.ToJson();

    EXPECT_EQ(json["storage_used"].asUInt64(), 0ULL);
    EXPECT_EQ(json["storage_quota"].asUInt64(), 0ULL);
    EXPECT_EQ(json["storage_reserved"].asUInt64(), 0ULL);
}

// ==================== UserListResponse Tests ====================

TEST(UserListResponse, WithItems) {
    UserListResponse response;
    response.items.resize(2);
    response.items[0].id = 1;
    response.items[0].username = "alice";
    response.items[0].email = "alice@test.com";
    response.items[1].id = 2;
    response.items[1].username = "bob";
    response.items[1].email = "bob@test.com";
    response.pagination.page = 1;
    response.pagination.page_size = 20;
    response.pagination.total = 2;
    response.pagination.total_pages = 1;

    auto json = response.ToJson();

    EXPECT_TRUE(json["items"].isArray());
    EXPECT_EQ(json["items"].size(), 2u);
    EXPECT_EQ(json["items"][0]["username"].asString(), "alice");
    EXPECT_EQ(json["items"][1]["username"].asString(), "bob");
    EXPECT_TRUE(json.isMember("pagination"));
    EXPECT_EQ(json["pagination"]["total"].asInt(), 2);
}

TEST(UserListResponse, EmptyItems) {
    UserListResponse response;
    response.pagination.page = 1;
    response.pagination.page_size = 20;
    response.pagination.total = 0;
    response.pagination.total_pages = 0;

    auto json = response.ToJson();

    EXPECT_TRUE(json["items"].isArray());
    EXPECT_EQ(json["items"].size(), 0u);
    EXPECT_EQ(json["pagination"]["total"].asInt(), 0);
}

// ==================== StorageStatsResponse Tests ====================

TEST(StorageStatsResponse, AllFields) {
    StorageStatsResponse response;
    response.total_users = 100;
    response.total_files = 500;
    response.total_storage_used = 1024;
    response.total_storage_quota = 1048576;
    response.active_shares = 10;

    auto json = response.ToJson();

    EXPECT_EQ(json["total_users"].asInt(), 100);
    EXPECT_EQ(json["total_files"].asInt(), 500);
    EXPECT_EQ(json["total_storage_used"].asUInt64(), 1024ULL);
    EXPECT_EQ(json["total_storage_quota"].asUInt64(), 1048576ULL);
    EXPECT_EQ(json["active_shares"].asInt(), 10);
}

TEST(StorageStatsResponse, ZeroValues) {
    StorageStatsResponse response;

    auto json = response.ToJson();

    EXPECT_EQ(json["total_users"].asInt(), 0);
    EXPECT_EQ(json["total_files"].asInt(), 0);
    EXPECT_EQ(json["total_storage_used"].asUInt64(), 0ULL);
    EXPECT_EQ(json["total_storage_quota"].asUInt64(), 0ULL);
    EXPECT_EQ(json["active_shares"].asInt(), 0);
}

// ==================== SystemStatusResponse Tests ====================

TEST(SystemStatusResponse, AllConnected) {
    SystemStatusResponse response;
    response.mysql_connected = true;
    response.redis_connected = true;
    response.disk_total = 107374182400;
    response.disk_used = 53687091200;
    response.disk_free = 53687091200;
    response.uptime_seconds = 3600;

    auto json = response.ToJson();

    EXPECT_TRUE(json["mysql_connected"].asBool());
    EXPECT_TRUE(json["redis_connected"].asBool());
    EXPECT_EQ(json["disk_total"].asUInt64(), 107374182400ULL);
    EXPECT_EQ(json["disk_used"].asUInt64(), 53687091200ULL);
    EXPECT_EQ(json["disk_free"].asUInt64(), 53687091200ULL);
    EXPECT_EQ(json["uptime_seconds"].asUInt64(), 3600ULL);
}

TEST(SystemStatusResponse, Disconnected) {
    SystemStatusResponse response;
    response.mysql_connected = false;
    response.redis_connected = false;

    auto json = response.ToJson();

    EXPECT_FALSE(json["mysql_connected"].asBool());
    EXPECT_FALSE(json["redis_connected"].asBool());
}

TEST(SystemStatusResponse, ZeroDisk) {
    SystemStatusResponse response;
    response.disk_total = 0;
    response.disk_used = 0;
    response.disk_free = 0;
    response.uptime_seconds = 0;

    auto json = response.ToJson();

    EXPECT_EQ(json["disk_total"].asUInt64(), 0ULL);
    EXPECT_EQ(json["disk_used"].asUInt64(), 0ULL);
    EXPECT_EQ(json["disk_free"].asUInt64(), 0ULL);
    EXPECT_EQ(json["uptime_seconds"].asUInt64(), 0ULL);
}

TEST(SystemStatusResponse, MixedConnectivity) {
    SystemStatusResponse response;
    response.mysql_connected = true;
    response.redis_connected = false;
    response.disk_total = 1000;
    response.disk_used = 500;
    response.disk_free = 500;
    response.uptime_seconds = 86400;

    auto json = response.ToJson();

    EXPECT_TRUE(json["mysql_connected"].asBool());
    EXPECT_FALSE(json["redis_connected"].asBool());
    EXPECT_EQ(json["uptime_seconds"].asUInt64(), 86400ULL);
}

// ==================== ShareDetailResponse Tests ====================

TEST(ShareDetailResponse, AllFields) {
    ShareDetailResponse response;
    response.id = 1;
    response.user_id = 42;
    response.username = "alice";
    response.file_id = 10;
    response.file_name = "doc.pdf";
    response.share_code = "abc123";
    response.status = 1;
    response.access_count = 5;
    response.created_at = "2026-01-15T10:00:00Z";
    response.expires_at = "2026-02-15T10:00:00Z";

    auto json = response.ToJson();

    EXPECT_EQ(json["id"].asUInt64(), 1);
    EXPECT_EQ(json["user_id"].asUInt64(), 42);
    EXPECT_EQ(json["username"].asString(), "alice");
    EXPECT_EQ(json["file_id"].asUInt64(), 10);
    EXPECT_EQ(json["file_name"].asString(), "doc.pdf");
    EXPECT_EQ(json["share_code"].asString(), "abc123");
    EXPECT_EQ(json["status"].asInt(), 1);
    EXPECT_EQ(json["access_count"].asInt(), 5);
    EXPECT_EQ(json["created_at"].asString(), "2026-01-15T10:00:00Z");
    EXPECT_EQ(json["expires_at"].asString(), "2026-02-15T10:00:00Z");
}

TEST(ShareDetailResponse, NullableFileName) {
    ShareDetailResponse response;
    response.id = 1;
    response.user_id = 1;
    response.username = "bob";
    response.file_id = 1;
    response.file_name = "";
    response.share_code = "xyz";
    response.created_at = "";
    response.expires_at = "";

    auto json = response.ToJson();

    EXPECT_EQ(json["file_name"].asString(), "");
    EXPECT_EQ(json["created_at"].asString(), "");
    EXPECT_EQ(json["expires_at"].asString(), "");
}

// ==================== ShareListResponse Tests ====================

TEST(ShareListResponse, WithItems) {
    ShareListResponse response;
    response.items.resize(2);
    response.items[0].id = 1;
    response.items[0].username = "alice";
    response.items[0].share_code = "abc";
    response.items[1].id = 2;
    response.items[1].username = "bob";
    response.items[1].share_code = "def";
    response.pagination.page = 1;
    response.pagination.page_size = 20;
    response.pagination.total = 2;
    response.pagination.total_pages = 1;

    auto json = response.ToJson();

    EXPECT_TRUE(json["items"].isArray());
    EXPECT_EQ(json["items"].size(), 2u);
    EXPECT_EQ(json["items"][0]["username"].asString(), "alice");
    EXPECT_EQ(json["items"][1]["share_code"].asString(), "def");
    EXPECT_TRUE(json.isMember("pagination"));
    EXPECT_EQ(json["pagination"]["total"].asInt(), 2);
}

TEST(ShareListResponse, EmptyItems) {
    ShareListResponse response;
    response.pagination.page = 1;
    response.pagination.page_size = 20;
    response.pagination.total = 0;
    response.pagination.total_pages = 0;

    auto json = response.ToJson();

    EXPECT_TRUE(json["items"].isArray());
    EXPECT_EQ(json["items"].size(), 0u);
    EXPECT_EQ(json["pagination"]["total"].asInt(), 0);
}

// ==================== Error Code Contract Tests ====================

TEST(AdminErrorCodeContract, AllCodesHaveCorrectValues) {
    EXPECT_EQ(static_cast<int>(Code::AdminRequired), 80001);
    EXPECT_EQ(static_cast<int>(Code::AdminUserNotFound), 80002);
    EXPECT_EQ(static_cast<int>(Code::AdminCannotModifySelf), 80003);
    EXPECT_EQ(static_cast<int>(Code::AdminCannotDemoteLast), 80004);
    EXPECT_EQ(static_cast<int>(Code::AdminShareNotFound), 80005);
    EXPECT_EQ(static_cast<int>(Code::AdminInvalidStatus), 80006);
    EXPECT_EQ(static_cast<int>(Code::AdminInvalidRole), 80007);
}

TEST(AdminErrorCodeContract, CodesAreDistinct) {
    std::set<int> codes = {
        static_cast<int>(Code::AdminRequired),
        static_cast<int>(Code::AdminUserNotFound),
        static_cast<int>(Code::AdminCannotModifySelf),
        static_cast<int>(Code::AdminCannotDemoteLast),
        static_cast<int>(Code::AdminShareNotFound),
        static_cast<int>(Code::AdminInvalidStatus),
        static_cast<int>(Code::AdminInvalidRole),
    };
    EXPECT_EQ(codes.size(), 7u) << "All admin error codes must be distinct";
}

TEST(AdminErrorCodeContract, CodesAreInRange) {
    auto check = [](Code code) {
        auto val = static_cast<int>(code);
        EXPECT_GE(val, 80001);
        EXPECT_LE(val, 80099);
    };
    check(Code::AdminRequired);
    check(Code::AdminUserNotFound);
    check(Code::AdminCannotModifySelf);
    check(Code::AdminCannotDemoteLast);
    check(Code::AdminShareNotFound);
    check(Code::AdminInvalidStatus);
    check(Code::AdminInvalidRole);
}
