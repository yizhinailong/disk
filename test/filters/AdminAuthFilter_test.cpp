/**
 * @file AdminAuthFilter_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief AdminAuthFilter 单元测试
 *
 * @copyright Copyright (c) 2026
 *
 * 测试管理员权限过滤器的全部决策路径：
 * 1. 非管理员路径（非 /api/admin/ 子路径）始终放行
 * 2. 管理员路径 + admin 角色 + active 状态 → 放行
 * 3. 管理员路径 + 非 admin 角色 → 拒绝 (403)
 * 4. 管理员路径 + 非 active 状态 → 拒绝 (403)
 * 5. 错误响应体验证（code + message）
 * 6. 错误码 HTTP 状态映射契约
 */

#include <string>

#include <drogon/HttpFilter.h>
#include <drogon/HttpRequest.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>

#include "filters/AdminAuthFilter.hpp"

#include "utils/ErrorCode.hpp"
#include "utils/Response.hpp"

namespace {

    using disk::error::Code;

    /// ================================================================================
    /// Helper: 创建带有路径、角色和状态的请求
    /// ================================================================================

    auto CreateRequest(
        const std::string& path,
        int role = 1,
        int status = 1,
        uint64_t user_id = 1
    ) -> drogon::HttpRequestPtr {
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setPath(path);
        req->attributes()->insert("user_id", user_id);
        req->attributes()->insert("username", std::string{"admin"});
        req->attributes()->insert("role", role);
        req->attributes()->insert("status", status);
        return req;
    }

} ///< namespace

/// ================================================================================
/// 决策矩阵测试（管理员路径 /api/admin/*）
/// ================================================================================

TEST(AdminAuthFilterTest, AdminPath_AdminRole_ActiveStatus_Pass) {
    disk::filters::AdminAuthFilter filter;
    auto req = CreateRequest("/api/admin/users", 1, 1);
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(AdminAuthFilterTest, AdminPath_UserRole_ActiveStatus_Reject) {
    disk::filters::AdminAuthFilter filter;
    auto req = CreateRequest("/api/admin/users", 0, 1);
    auto resp = drogon::sync_wait(filter.doFilter(req));
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::k403Forbidden);
}

TEST(AdminAuthFilterTest, AdminPath_AdminRole_DisabledStatus_Reject) {
    disk::filters::AdminAuthFilter filter;
    auto req = CreateRequest("/api/admin/users", 1, 0);
    auto resp = drogon::sync_wait(filter.doFilter(req));
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::k403Forbidden);
}

TEST(AdminAuthFilterTest, AdminPath_UserRole_DisabledStatus_Reject) {
    disk::filters::AdminAuthFilter filter;
    auto req = CreateRequest("/api/admin/users", 0, 0);
    auto resp = drogon::sync_wait(filter.doFilter(req));
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::k403Forbidden);
}

TEST(AdminAuthFilterTest, AdminPath_AdminRole_LockedStatus_Reject) {
    disk::filters::AdminAuthFilter filter;
    auto req = CreateRequest("/api/admin/users", 1, 2);
    auto resp = drogon::sync_wait(filter.doFilter(req));
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::k403Forbidden);
}

TEST(AdminAuthFilterTest, AdminPath_InvalidRole2_ActiveStatus_Reject) {
    disk::filters::AdminAuthFilter filter;
    auto req = CreateRequest("/api/admin/users", 2, 1);
    auto resp = drogon::sync_wait(filter.doFilter(req));
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::k403Forbidden);
}

TEST(AdminAuthFilterTest, AdminPath_RoleCheckedBeforeStatus) {
    /// role=0, status=0 → 应先因 role 拒绝（两个条件都不满足，但 role 检查在前）
    disk::filters::AdminAuthFilter filter;
    auto req = CreateRequest("/api/admin/users", 0, 0);
    auto resp = drogon::sync_wait(filter.doFilter(req));
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::k403Forbidden);
}

/// ================================================================================
/// 路径前缀测试（非管理员路径始终放行）
/// ================================================================================

TEST(AdminAuthFilterTest, AuthLoginPath_PassRegardlessOfRole) {
    disk::filters::AdminAuthFilter filter;
    auto req = CreateRequest("/api/auth/login", 0, 0);
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(AdminAuthFilterTest, HealthPath_PassRegardlessOfRole) {
    disk::filters::AdminAuthFilter filter;
    auto req = CreateRequest("/api/health", 0, 0);
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(AdminAuthFilterTest, FileListPath_PassRegardlessOfRole) {
    disk::filters::AdminAuthFilter filter;
    auto req = CreateRequest("/api/file/list", 0, 0);
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(AdminAuthFilterTest, ShareAccessPath_PassRegardlessOfRole) {
    disk::filters::AdminAuthFilter filter;
    auto req = CreateRequest("/api/share/access/abc123", 0, 0);
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(AdminAuthFilterTest, RootPath_Pass) {
    disk::filters::AdminAuthFilter filter;
    auto req = CreateRequest("/", 0, 0);
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(AdminAuthFilterTest, AdminPrefixExactPath_Pass) {
    /// /api/admin/ 精确匹配 starts_with("/api/admin/")
    disk::filters::AdminAuthFilter filter;
    auto req = CreateRequest("/api/admin/", 1, 1);
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(AdminAuthFilterTest, AdminPathWithoutTrailingSlash_Pass) {
    /// /api/admin 不以 "/" 结尾，不匹配 starts_with("/api/admin/")
    disk::filters::AdminAuthFilter filter;
    auto req = CreateRequest("/api/admin", 0, 0);
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

/// ================================================================================
/// 管理员子路径测试（所有 /api/admin/* 路径均受检查）
/// ================================================================================

TEST(AdminAuthFilterTest, AdminSharesPath_AdminPass) {
    disk::filters::AdminAuthFilter filter;
    auto req = CreateRequest("/api/admin/shares", 1, 1);
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(AdminAuthFilterTest, AdminStatsPath_AdminPass) {
    disk::filters::AdminAuthFilter filter;
    auto req = CreateRequest("/api/admin/stats/overview", 1, 1);
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(AdminAuthFilterTest, AdminUserDetailPath_AdminPass) {
    disk::filters::AdminAuthFilter filter;
    auto req = CreateRequest("/api/admin/users/42", 1, 1);
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(AdminAuthFilterTest, AdminStorageStatsPath_AdminPass) {
    disk::filters::AdminAuthFilter filter;
    auto req = CreateRequest("/api/admin/storage/stats", 1, 1);
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(AdminAuthFilterTest, AdminSharesPath_NonAdminReject) {
    disk::filters::AdminAuthFilter filter;
    auto req = CreateRequest("/api/admin/shares", 0, 1);
    auto resp = drogon::sync_wait(filter.doFilter(req));
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::k403Forbidden);
}

TEST(AdminAuthFilterTest, AdminStatsPath_NonAdminReject) {
    disk::filters::AdminAuthFilter filter;
    auto req = CreateRequest("/api/admin/stats/overview", 0, 1);
    auto resp = drogon::sync_wait(filter.doFilter(req));
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::k403Forbidden);
}

/// ================================================================================
/// 错误响应体验证
/// ================================================================================

TEST(AdminAuthFilterTest, RejectResponse_ContainsErrorCode) {
    disk::filters::AdminAuthFilter filter;
    auto req = CreateRequest("/api/admin/users", 0, 1);
    auto resp = drogon::sync_wait(filter.doFilter(req));
    ASSERT_NE(resp, nullptr);
    auto json = resp->getJsonObject();
    ASSERT_NE(json, nullptr);
    EXPECT_EQ((*json)["code"].asUInt(), static_cast<Json::UInt>(80001));
}

TEST(AdminAuthFilterTest, RejectResponse_ContainsMessage) {
    disk::filters::AdminAuthFilter filter;
    auto req = CreateRequest("/api/admin/users", 0, 1);
    auto resp = drogon::sync_wait(filter.doFilter(req));
    ASSERT_NE(resp, nullptr);
    auto json = resp->getJsonObject();
    ASSERT_NE(json, nullptr);
    EXPECT_FALSE((*json)["message"].asString().empty());
}

TEST(AdminAuthFilterTest, RejectResponse_ContainsNullData) {
    disk::filters::AdminAuthFilter filter;
    auto req = CreateRequest("/api/admin/users", 0, 1);
    auto resp = drogon::sync_wait(filter.doFilter(req));
    ASSERT_NE(resp, nullptr);
    auto json = resp->getJsonObject();
    ASSERT_NE(json, nullptr);
    EXPECT_TRUE((*json)["data"].isNull());
}

TEST(AdminAuthFilterTest, RejectResponse_DisabledStatus_SameErrorCode) {
    /// 无论因 role 还是 status 拒绝，都返回 AdminRequired (80001)
    disk::filters::AdminAuthFilter filter;
    auto req = CreateRequest("/api/admin/users", 1, 0);
    auto resp = drogon::sync_wait(filter.doFilter(req));
    ASSERT_NE(resp, nullptr);
    auto json = resp->getJsonObject();
    ASSERT_NE(json, nullptr);
    EXPECT_EQ((*json)["code"].asUInt(), static_cast<Json::UInt>(80001));
}

/// ================================================================================
/// 错误码 HTTP 状态映射契约测试
/// ================================================================================

TEST(AdminAuthFilterTest, AdminRequired_MapsTo403) {
    EXPECT_EQ(disk::error::GetHttpStatus(Code::AdminRequired), drogon::k403Forbidden);
}

TEST(AdminAuthFilterTest, AdminUserNotFound_MapsTo404) {
    EXPECT_EQ(disk::error::GetHttpStatus(Code::AdminUserNotFound), drogon::k404NotFound);
}

TEST(AdminAuthFilterTest, AdminCannotModifySelf_MapsTo400) {
    EXPECT_EQ(disk::error::GetHttpStatus(Code::AdminCannotModifySelf), drogon::k400BadRequest);
}

TEST(AdminAuthFilterTest, AdminCannotDemoteLast_MapsTo400) {
    EXPECT_EQ(disk::error::GetHttpStatus(Code::AdminCannotDemoteLast), drogon::k400BadRequest);
}

TEST(AdminAuthFilterTest, AdminShareNotFound_MapsTo404) {
    EXPECT_EQ(disk::error::GetHttpStatus(Code::AdminShareNotFound), drogon::k404NotFound);
}

TEST(AdminAuthFilterTest, AdminInvalidStatus_MapsTo400) {
    EXPECT_EQ(disk::error::GetHttpStatus(Code::AdminInvalidStatus), drogon::k400BadRequest);
}

TEST(AdminAuthFilterTest, AdminInvalidRole_MapsTo400) {
    EXPECT_EQ(disk::error::GetHttpStatus(Code::AdminInvalidRole), drogon::k400BadRequest);
}

/// ================================================================================
/// 过滤器无状态测试（多次调用互不影响）
/// ================================================================================

TEST(AdminAuthFilterTest, FilterIsStateless_MultipleSequentialCalls) {
    disk::filters::AdminAuthFilter filter;

    /// 第一次调用：admin 放行
    auto req1 = CreateRequest("/api/admin/users", 1, 1);
    auto resp1 = drogon::sync_wait(filter.doFilter(req1));
    EXPECT_EQ(resp1, nullptr);

    /// 第二次调用：非 admin 拒绝
    auto req2 = CreateRequest("/api/admin/users", 0, 1);
    auto resp2 = drogon::sync_wait(filter.doFilter(req2));
    ASSERT_NE(resp2, nullptr);
    EXPECT_EQ(resp2->getStatusCode(), drogon::k403Forbidden);

    /// 第三次调用：admin 再次放行
    auto req3 = CreateRequest("/api/admin/stats", 1, 1);
    auto resp3 = drogon::sync_wait(filter.doFilter(req3));
    EXPECT_EQ(resp3, nullptr);
}

/// ================================================================================
/// 不同用户 ID 测试（user_id 不影响过滤决策）
/// ================================================================================

TEST(AdminAuthFilterTest, DifferentUserIds_SameRoleStatus_SameResult) {
    disk::filters::AdminAuthFilter filter;

    auto req1 = CreateRequest("/api/admin/users", 1, 1, 1);
    auto resp1 = drogon::sync_wait(filter.doFilter(req1));
    EXPECT_EQ(resp1, nullptr);

    auto req2 = CreateRequest("/api/admin/users", 1, 1, 999);
    auto resp2 = drogon::sync_wait(filter.doFilter(req2));
    EXPECT_EQ(resp2, nullptr);

    auto req3 = CreateRequest("/api/admin/users", 0, 1, 42);
    auto resp3 = drogon::sync_wait(filter.doFilter(req3));
    ASSERT_NE(resp3, nullptr);
    EXPECT_EQ(resp3->getStatusCode(), drogon::k403Forbidden);
}
