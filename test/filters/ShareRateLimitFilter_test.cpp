/**
 * @file ShareRateLimitFilter_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Route-owned share operation rate-limit filter tests
 *
 * @copyright Copyright (c) 2026
 */

#include "filters/ShareRateLimitFilter.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>

#include "filters/ShareAuthFilter.hpp"
#include "utils/ErrorCode.hpp"

namespace {

    using disk::error::Code;
    using disk::filters::ShareAccessRateLimitFilter;
    using disk::filters::ShareAuthFilter;
    using disk::filters::ShareOperationRateLimitFilter;
    using disk::filters::ShareRateLimitCounter;

    struct CounterCall {
        std::string key;
        int window_seconds{};
    };

    struct CounterState {
        std::vector<Result<int64_t>> results;
        std::vector<CounterCall> calls;
        size_t next_result{};
    };

    auto MakeCounter(const std::shared_ptr<CounterState>& state) -> ShareRateLimitCounter {
        return [state](const std::string& key, int window_seconds)
                   -> drogon::Task<Result<int64_t>> {
            state->calls.push_back(CounterCall{ .key = key, .window_seconds = window_seconds });
            if (state->next_result >= state->results.size()) {
                co_return int64_t{ 1 };
            }

            const auto& result = state->results[state->next_result++];
            if (!result) {
                co_return std::unexpected(result.error());
            }
            co_return *result;
        };
    }

    auto AddCount(const std::shared_ptr<CounterState>& state, int64_t count) -> void {
        state->results.emplace_back(count);
    }

    auto AddFailure(const std::shared_ptr<CounterState>& state) -> void {
        state->results.emplace_back(std::unexpected(ErrorInfo(Code::RedisOperationFailed, "injected counter failure")));
    }

    auto CreateRequest(const std::string& path) -> drogon::HttpRequestPtr {
        auto request = drogon::HttpRequest::newHttpRequest();
        request->setPath(path);
        return request;
    }

    auto CreateOperationRequest(
        const std::string& path,
        const std::string& jti = "verified-jti"
    ) -> drogon::HttpRequestPtr {
        auto request = CreateRequest(path);
        request->addHeader("X-Share-Token", "raw-replayable-share-token");
        request->attributes()->insert(ShareAuthFilter::SHARE_TOKEN_JTI_ATTRIBUTE, jti);
        return request;
    }

    auto ExpectRateLimited(const drogon::HttpResponsePtr& response, int limit) -> void {
        ASSERT_NE(response, nullptr);
        EXPECT_EQ(response->getStatusCode(), drogon::k429TooManyRequests);

        const auto json = response->getJsonObject();
        ASSERT_NE(json, nullptr);
        EXPECT_EQ((*json)["code"].asUInt(), static_cast<Json::UInt>(Code::TooManyRequests));
        EXPECT_EQ((*json)["message"].asString(), "Too many requests");
        EXPECT_TRUE((*json)["data"].isNull());

        EXPECT_EQ(response->getHeader("X-RateLimit-Limit"), std::to_string(limit));
        EXPECT_EQ(response->getHeader("X-RateLimit-Remaining"), "0");
        EXPECT_FALSE(response->getHeader("X-RateLimit-Reset").empty());
        EXPECT_FALSE(response->getHeader("Retry-After").empty());
    }

    TEST(ShareRateLimitFilterTest, AccessFilterSkipsUnrelatedRoutes) {
        const auto state = std::make_shared<CounterState>();
        ShareAccessRateLimitFilter filter(MakeCounter(state));

        const auto response = drogon::sync_wait(filter.doFilter(CreateRequest("/api/share")));

        EXPECT_EQ(response, nullptr);
        EXPECT_TRUE(state->calls.empty());
    }

    TEST(ShareRateLimitFilterTest, AccessFilterUsesCentralKeyAndDefaultWindow) {
        const auto state = std::make_shared<CounterState>();
        ShareAccessRateLimitFilter filter(MakeCounter(state));
        auto request = CreateRequest("/api/share/access/share-code");

        const auto response = drogon::sync_wait(filter.doFilter(request));

        EXPECT_EQ(response, nullptr);
        ASSERT_EQ(state->calls.size(), 1U);
        EXPECT_TRUE(state->calls.front().key.starts_with("rate:share_access:"));
        EXPECT_EQ(state->calls.front().window_seconds, 60);
    }

    TEST(ShareRateLimitFilterTest, AccessBoundaryReturnsStandard429) {
        const auto state = std::make_shared<CounterState>();
        AddCount(state, 30);
        AddCount(state, 31);
        ShareAccessRateLimitFilter filter(MakeCounter(state));

        const auto allowed = drogon::sync_wait(
            filter.doFilter(CreateRequest("/api/share/access/share-code"))
        );
        const auto limited = drogon::sync_wait(
            filter.doFilter(CreateRequest("/api/share/access/share-code"))
        );

        EXPECT_EQ(allowed, nullptr);
        ExpectRateLimited(limited, 30);
        EXPECT_EQ(state->calls.size(), 2U);
    }

    TEST(ShareRateLimitFilterTest, OperationFilterSkipsUnrelatedRoutes) {
        const auto state = std::make_shared<CounterState>();
        ShareOperationRateLimitFilter filter(MakeCounter(state));

        const auto response = drogon::sync_wait(
            filter.doFilter(CreateOperationRequest("/api/share/detail/share-code"))
        );

        EXPECT_EQ(response, nullptr);
        EXPECT_TRUE(state->calls.empty());
    }

    TEST(ShareRateLimitFilterTest, OperationFilterRequiresVerifiedJtiAttribute) {
        const auto state = std::make_shared<CounterState>();
        ShareOperationRateLimitFilter filter(MakeCounter(state));

        const auto missing = drogon::sync_wait(
            filter.doFilter(CreateRequest("/api/share/browse/share-code"))
        );
        const auto empty = drogon::sync_wait(
            filter.doFilter(CreateOperationRequest("/api/share/browse/share-code", ""))
        );

        EXPECT_EQ(missing, nullptr);
        EXPECT_EQ(empty, nullptr);
        EXPECT_TRUE(state->calls.empty());
    }

    TEST(ShareRateLimitFilterTest, BrowseUsesJtiWithoutReadingRawToken) {
        const auto state = std::make_shared<CounterState>();
        ShareOperationRateLimitFilter filter(MakeCounter(state));

        const auto response = drogon::sync_wait(
            filter.doFilter(CreateOperationRequest("/api/share/browse/share-code"))
        );

        EXPECT_EQ(response, nullptr);
        ASSERT_EQ(state->calls.size(), 1U);
        EXPECT_TRUE(state->calls.front().key.starts_with("rate:share_browse:verified-jti:"));
        EXPECT_EQ(state->calls.front().key.find("raw-replayable-share-token"), std::string::npos);
        EXPECT_EQ(state->calls.front().window_seconds, 60);
    }

    TEST(ShareRateLimitFilterTest, DownloadRoutesRangeAndRetryShareOneBucket) {
        const auto state = std::make_shared<CounterState>();
        ShareOperationRateLimitFilter filter(MakeCounter(state));

        const std::vector<std::string> paths{
            "/api/share/download/share-code/7/info",
            "/api/share/download/share-code/7",
            "/api/share/download/share-code/7",
            "/api/share/download/share-code/7",
            "/api/share/save/share-code",
        };
        for (size_t index = 0; index < paths.size(); ++index) {
            auto request = CreateOperationRequest(paths[index]);
            if (index == 2) {
                request->addHeader("Range", "bytes=1024-");
            }
            EXPECT_EQ(drogon::sync_wait(filter.doFilter(request)), nullptr);
        }

        ASSERT_EQ(state->calls.size(), paths.size());
        const auto& shared_key = state->calls.front().key;
        EXPECT_TRUE(shared_key.starts_with("rate:share_download:verified-jti:"));
        for (const auto& call : state->calls) {
            EXPECT_EQ(call.key, shared_key);
            EXPECT_EQ(call.window_seconds, 60);
        }
    }

    TEST(ShareRateLimitFilterTest, OperationsAndJtisUseIsolatedKeys) {
        const auto state = std::make_shared<CounterState>();
        ShareOperationRateLimitFilter filter(MakeCounter(state));

        EXPECT_EQ(
            drogon::sync_wait(filter.doFilter(
                CreateOperationRequest("/api/share/browse/share-code", "jti-one")
            )),
            nullptr
        );
        EXPECT_EQ(
            drogon::sync_wait(filter.doFilter(
                CreateOperationRequest("/api/share/download/share-code/7", "jti-one")
            )),
            nullptr
        );
        EXPECT_EQ(
            drogon::sync_wait(filter.doFilter(
                CreateOperationRequest("/api/share/browse/share-code", "jti-two")
            )),
            nullptr
        );

        ASSERT_EQ(state->calls.size(), 3U);
        EXPECT_NE(state->calls[0].key, state->calls[1].key);
        EXPECT_NE(state->calls[0].key, state->calls[2].key);
        EXPECT_NE(state->calls[1].key, state->calls[2].key);
    }

    TEST(ShareRateLimitFilterTest, BrowseBoundaryReturnsStandard429) {
        const auto state = std::make_shared<CounterState>();
        AddCount(state, 60);
        AddCount(state, 61);
        ShareOperationRateLimitFilter filter(MakeCounter(state));

        const auto allowed = drogon::sync_wait(
            filter.doFilter(CreateOperationRequest("/api/share/browse/share-code"))
        );
        const auto limited = drogon::sync_wait(
            filter.doFilter(CreateOperationRequest("/api/share/browse/share-code"))
        );

        EXPECT_EQ(allowed, nullptr);
        ExpectRateLimited(limited, 60);
    }

    TEST(ShareRateLimitFilterTest, DownloadBoundaryReturnsStandard429) {
        const auto state = std::make_shared<CounterState>();
        AddCount(state, 10);
        AddCount(state, 11);
        ShareOperationRateLimitFilter filter(MakeCounter(state));

        const auto allowed = drogon::sync_wait(
            filter.doFilter(CreateOperationRequest("/api/share/download/share-code/7/info"))
        );
        const auto limited = drogon::sync_wait(
            filter.doFilter(CreateOperationRequest("/api/share/save/share-code"))
        );

        EXPECT_EQ(allowed, nullptr);
        ExpectRateLimited(limited, 10);
        ASSERT_EQ(state->calls.size(), 2U);
        EXPECT_EQ(state->calls[0].key, state->calls[1].key);
    }

    TEST(ShareRateLimitFilterTest, RedisFailuresFailOpenForBothFilters) {
        const auto access_state = std::make_shared<CounterState>();
        AddFailure(access_state);
        ShareAccessRateLimitFilter access_filter(MakeCounter(access_state));

        const auto operation_state = std::make_shared<CounterState>();
        AddFailure(operation_state);
        ShareOperationRateLimitFilter operation_filter(MakeCounter(operation_state));

        const auto access_response = drogon::sync_wait(
            access_filter.doFilter(CreateRequest("/api/share/access/share-code"))
        );
        const auto operation_response = drogon::sync_wait(
            operation_filter.doFilter(
                CreateOperationRequest("/api/share/download/share-code/7")
            )
        );

        EXPECT_EQ(access_response, nullptr);
        EXPECT_EQ(operation_response, nullptr);
        EXPECT_EQ(access_state->calls.size(), 1U);
        EXPECT_EQ(operation_state->calls.size(), 1U);
    }

} // namespace
