/**
 * @file RegisterRateLimit_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief RegisterRateLimitFilter path-matching tests
 *
 * @copyright Copyright (c) 2026
 */

#include <string>

#include <drogon/HttpFilter.h>
#include <drogon/HttpRequest.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>

#include "filters/RegisterRateLimitFilter.hpp"

#include "utils/ErrorCode.hpp"

namespace {

    auto CreateRequest(const std::string& path) -> drogon::HttpRequestPtr {
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setPath(path);
        return req;
    }

} // namespace

TEST(RegisterRateLimitFilterTest, LoginPath_Skipped) {
    disk::filters::RegisterRateLimitFilter filter;
    auto req = CreateRequest("/api/auth/login");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(RegisterRateLimitFilterTest, RefreshPath_Skipped) {
    disk::filters::RegisterRateLimitFilter filter;
    auto req = CreateRequest("/api/auth/refresh");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(RegisterRateLimitFilterTest, HealthPath_Skipped) {
    disk::filters::RegisterRateLimitFilter filter;
    auto req = CreateRequest("/api/health");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(RegisterRateLimitFilterTest, FileListPath_Skipped) {
    disk::filters::RegisterRateLimitFilter filter;
    auto req = CreateRequest("/api/file/list");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(RegisterRateLimitFilterTest, ExactMatchRequired) {
    disk::filters::RegisterRateLimitFilter filter;
    auto req = CreateRequest("/api/auth/register/extra");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(RegisterRateLimitFilterTest, PartialMatchNotEnough) {
    disk::filters::RegisterRateLimitFilter filter;
    auto req = CreateRequest("/api/auth/regis");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(RegisterRateLimitFilterTest, SharePath_Skipped) {
    disk::filters::RegisterRateLimitFilter filter;
    auto req = CreateRequest("/api/share/access/abc");
    auto resp = drogon::sync_wait(filter.doFilter(req));
    EXPECT_EQ(resp, nullptr);
}

TEST(RegisterRateLimitFilterTest, DefaultLimit_Is5) {
    EXPECT_EQ(disk::filters::RegisterRateLimitFilter::DEFAULT_LIMIT, 5);
}

TEST(RegisterRateLimitFilterTest, WindowSeconds_Is300) {
    EXPECT_EQ(disk::filters::RegisterRateLimitFilter::WINDOW_SECONDS, 300);
}

TEST(RegisterRateLimitFilterTest, TooManyRequests_MapsTo429) {
    EXPECT_EQ(
        disk::error::GetHttpStatus(disk::error::Code::TooManyRequests),
        drogon::k429TooManyRequests
    );
}
