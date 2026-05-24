/**
 * @file DownloadRateLimit_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief DownloadRateLimitFilter path-matching tests
 *
 * @copyright Copyright (c) 2026
 */

#include <string>

#include <drogon/HttpFilter.h>
#include <drogon/HttpRequest.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>

#include "filters/DownloadRateLimitFilter.hpp"

namespace {

    auto CreateRequest(const std::string& path, uint64_t user_id = 1) -> drogon::HttpRequestPtr {
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setPath(path);
        req->attributes()->insert("user_id", user_id);
        return req;
    }

} // namespace

TEST(DownloadRateLimitTest, FileListPath_Skipped) {
    disk::filters::DownloadRateLimitFilter filter;
    auto req = CreateRequest("/api/file/list");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(DownloadRateLimitTest, FileDetailPath_Skipped) {
    disk::filters::DownloadRateLimitFilter filter;
    auto req = CreateRequest("/api/file/123");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(DownloadRateLimitTest, UploadPath_Skipped) {
    disk::filters::DownloadRateLimitFilter filter;
    auto req = CreateRequest("/api/file/upload/init");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(DownloadRateLimitTest, MovePath_Skipped) {
    disk::filters::DownloadRateLimitFilter filter;
    auto req = CreateRequest("/api/file/move");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(DownloadRateLimitTest, SearchPath_Skipped) {
    disk::filters::DownloadRateLimitFilter filter;
    auto req = CreateRequest("/api/file/search");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(DownloadRateLimitTest, DownloadPrefixWithoutTrailing_Skipped) {
    disk::filters::DownloadRateLimitFilter filter;
    auto req = CreateRequest("/api/file/download");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(DownloadRateLimitTest, NoUserId_Skipped) {
    disk::filters::DownloadRateLimitFilter filter;
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setPath("/api/file/download/123");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(DownloadRateLimitTest, DefaultLimit_Is60) {
    EXPECT_EQ(disk::filters::DownloadRateLimitFilter::DEFAULT_LIMIT, 60);
}

TEST(DownloadRateLimitTest, WindowSeconds_Is60) {
    EXPECT_EQ(disk::filters::DownloadRateLimitFilter::WINDOW_SECONDS, 60);
}
