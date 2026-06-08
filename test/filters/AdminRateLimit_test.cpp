/**
 * @file AdminRateLimit_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief AdminRateLimitFilter path-matching tests
 *
 * @copyright Copyright (c) 2026
 */

#include <string>

#include <drogon/HttpFilter.h>
#include <drogon/HttpRequest.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>

#include "filters/AdminRateLimitFilter.hpp"

namespace {

    auto CreateRequest(const std::string& path, uint64_t user_id = 1) -> drogon::HttpRequestPtr {
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setPath(path);
        req->attributes()->insert("user_id", user_id);
        return req;
    }

} ///< namespace

TEST(AdminRateLimitTest, AuthLoginPath_Skipped) {
    disk::filters::AdminRateLimitFilter filter;
    auto req = CreateRequest("/api/auth/login");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(AdminRateLimitTest, FileListPath_Skipped) {
    disk::filters::AdminRateLimitFilter filter;
    auto req = CreateRequest("/api/file/list");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(AdminRateLimitTest, AdminWithoutTrailingSlash_Skipped) {
    disk::filters::AdminRateLimitFilter filter;
    auto req = CreateRequest("/api/admin");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(AdminRateLimitTest, SharePath_Skipped) {
    disk::filters::AdminRateLimitFilter filter;
    auto req = CreateRequest("/api/share");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(AdminRateLimitTest, TrashPath_Skipped) {
    disk::filters::AdminRateLimitFilter filter;
    auto req = CreateRequest("/api/trash");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(AdminRateLimitTest, NoUserId_Skipped) {
    disk::filters::AdminRateLimitFilter filter;
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setPath("/api/admin/users");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(AdminRateLimitTest, DefaultLimit_Is30) {
    EXPECT_EQ(disk::filters::AdminRateLimitFilter::DEFAULT_LIMIT, 30);
}

TEST(AdminRateLimitTest, WindowSeconds_Is60) {
    EXPECT_EQ(disk::filters::AdminRateLimitFilter::WINDOW_SECONDS, 60);
}
