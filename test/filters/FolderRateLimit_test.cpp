/**
 * @file FolderRateLimit_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief FolderRateLimitFilter path-matching tests
 *
 * @copyright Copyright (c) 2026
 */

#include <string>

#include <drogon/HttpFilter.h>
#include <drogon/HttpRequest.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>

#include "filters/FolderRateLimitFilter.hpp"

namespace {

    auto CreateRequest(const std::string& path, uint64_t user_id = 1) -> drogon::HttpRequestPtr {
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setPath(path);
        req->attributes()->insert("user_id", user_id);
        return req;
    }

} // namespace

TEST(FolderRateLimitTest, AuthLoginPath_Skipped) {
    disk::filters::FolderRateLimitFilter filter;
    auto req = CreateRequest("/api/auth/login");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(FolderRateLimitTest, FileListPath_Skipped) {
    disk::filters::FolderRateLimitFilter filter;
    auto req = CreateRequest("/api/file/list");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(FolderRateLimitTest, AdminPath_Skipped) {
    disk::filters::FolderRateLimitFilter filter;
    auto req = CreateRequest("/api/admin/users");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(FolderRateLimitTest, FolderWithoutTrailingSlash_Skipped) {
    disk::filters::FolderRateLimitFilter filter;
    auto req = CreateRequest("/api/folder");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(FolderRateLimitTest, SharePath_Skipped) {
    disk::filters::FolderRateLimitFilter filter;
    auto req = CreateRequest("/api/share");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(FolderRateLimitTest, TrashPath_Skipped) {
    disk::filters::FolderRateLimitFilter filter;
    auto req = CreateRequest("/api/trash");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(FolderRateLimitTest, NoUserId_Skipped) {
    disk::filters::FolderRateLimitFilter filter;
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setPath("/api/folder/create");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(FolderRateLimitTest, DefaultLimit_Is100) {
    EXPECT_EQ(disk::filters::FolderRateLimitFilter::DEFAULT_LIMIT, 100);
}

TEST(FolderRateLimitTest, WindowSeconds_Is60) {
    EXPECT_EQ(disk::filters::FolderRateLimitFilter::WINDOW_SECONDS, 60);
}
