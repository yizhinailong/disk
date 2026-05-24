/**
 * @file SharePublicRateLimit_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief SharePublicRateLimitFilter path-matching tests
 *
 * @copyright Copyright (c) 2026
 */

#include <string>

#include <drogon/HttpFilter.h>
#include <drogon/HttpRequest.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>

#include "filters/SharePublicRateLimitFilter.hpp"

namespace {

    auto CreateRequest(const std::string& path) -> drogon::HttpRequestPtr {
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setPath(path);
        return req;
    }

} // namespace

TEST(SharePublicRateLimitTest, ShareListPath_Skipped) {
    disk::filters::SharePublicRateLimitFilter filter;
    auto req = CreateRequest("/api/share");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(SharePublicRateLimitTest, ShareDetailPath_Skipped) {
    disk::filters::SharePublicRateLimitFilter filter;
    auto req = CreateRequest("/api/share/abc123");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(SharePublicRateLimitTest, ShareCancelPath_Skipped) {
    disk::filters::SharePublicRateLimitFilter filter;
    auto req = CreateRequest("/api/share/cancel");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(SharePublicRateLimitTest, AuthPath_Skipped) {
    disk::filters::SharePublicRateLimitFilter filter;
    auto req = CreateRequest("/api/auth/login");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(SharePublicRateLimitTest, FileListPath_Skipped) {
    disk::filters::SharePublicRateLimitFilter filter;
    auto req = CreateRequest("/api/file/list");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(SharePublicRateLimitTest, AccessPrefixWithoutId_Skipped) {
    disk::filters::SharePublicRateLimitFilter filter;
    auto req = CreateRequest("/api/share/access");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(SharePublicRateLimitTest, BrowsePrefixWithoutId_Skipped) {
    disk::filters::SharePublicRateLimitFilter filter;
    auto req = CreateRequest("/api/share/browse");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(SharePublicRateLimitTest, DownloadPrefixWithoutId_Skipped) {
    disk::filters::SharePublicRateLimitFilter filter;
    auto req = CreateRequest("/api/share/download");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(SharePublicRateLimitTest, SavePath_Skipped) {
    disk::filters::SharePublicRateLimitFilter filter;
    auto req = CreateRequest("/api/share/save/abc123");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(SharePublicRateLimitTest, DefaultLimit_Is30) {
    EXPECT_EQ(disk::filters::SharePublicRateLimitFilter::DEFAULT_LIMIT, 30);
}

TEST(SharePublicRateLimitTest, WindowSeconds_Is60) {
    EXPECT_EQ(disk::filters::SharePublicRateLimitFilter::WINDOW_SECONDS, 60);
}

TEST(SharePublicRateLimitTest, IsSharePublicPath_AccessPath_True) {
    EXPECT_TRUE(disk::filters::SharePublicRateLimitFilter::IsSharePublicPath("/api/share/access/abc"));
}

TEST(SharePublicRateLimitTest, IsSharePublicPath_BrowsePath_True) {
    EXPECT_TRUE(disk::filters::SharePublicRateLimitFilter::IsSharePublicPath("/api/share/browse/abc"));
}

TEST(SharePublicRateLimitTest, IsSharePublicPath_DownloadPath_True) {
    EXPECT_TRUE(disk::filters::SharePublicRateLimitFilter::IsSharePublicPath("/api/share/download/abc/123"));
}

TEST(SharePublicRateLimitTest, IsSharePublicPath_OwnerShare_False) {
    EXPECT_FALSE(disk::filters::SharePublicRateLimitFilter::IsSharePublicPath("/api/share"));
}

TEST(SharePublicRateLimitTest, IsSharePublicPath_ShareDetail_False) {
    EXPECT_FALSE(disk::filters::SharePublicRateLimitFilter::IsSharePublicPath("/api/share/abc123"));
}

TEST(SharePublicRateLimitTest, IsSharePublicPath_AccessPrefix_False) {
    EXPECT_FALSE(disk::filters::SharePublicRateLimitFilter::IsSharePublicPath("/api/share/access"));
}
