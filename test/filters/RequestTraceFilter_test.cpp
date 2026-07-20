/**
 * @file RequestTraceFilter_test.cpp
 * @brief Request ID validation and propagation tests
 */

#include "filters/RequestTraceFilter.hpp"

#include <string>
#include <string_view>

#include <drogon/HttpFilter.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>

namespace disk::filters {
    namespace {
        auto RequestWithId(std::string_view request_id) -> drogon::HttpRequestPtr {
            auto request = drogon::HttpRequest::newHttpRequest();
            if (!request_id.empty()) {
                request->addHeader("X-Request-Id", std::string(request_id));
            }
            return request;
        }

        TEST(RequestTraceFilterTest, AcceptsSafeCallerRequestIdsAtLengthBoundary) {
            const std::string maximum_length_id(128, 'a');

            EXPECT_TRUE(RequestTraceFilter::IsValidRequestId("trace.API_01:attempt-2"));
            EXPECT_TRUE(RequestTraceFilter::IsValidRequestId(maximum_length_id));
            EXPECT_EQ(
                RequestTraceFilter::ResolveRequestId(
                    RequestWithId("trace.API_01:attempt-2")
                ),
                "trace.API_01:attempt-2"
            );
        }

        TEST(RequestTraceFilterTest, RejectsEmptyOversizedAndUnsafeCallerRequestIds) {
            EXPECT_FALSE(RequestTraceFilter::IsValidRequestId(""));
            EXPECT_FALSE(RequestTraceFilter::IsValidRequestId(std::string(129, 'a')));
            EXPECT_FALSE(RequestTraceFilter::IsValidRequestId("trace id"));
            EXPECT_FALSE(RequestTraceFilter::IsValidRequestId("trace/id"));
            EXPECT_FALSE(RequestTraceFilter::IsValidRequestId("trace\nforged"));
            EXPECT_FALSE(RequestTraceFilter::IsValidRequestId("trace\rforged"));
        }

        TEST(RequestTraceFilterTest, MissingOrUnsafeCallerIdFallsBackToValidUuid) {
            const auto missing = RequestTraceFilter::ResolveRequestId(RequestWithId(""));
            const auto unsafe =
                RequestTraceFilter::ResolveRequestId(RequestWithId("trace id"));

            EXPECT_EQ(missing.size(), 36U);
            EXPECT_EQ(unsafe.size(), 36U);
            EXPECT_TRUE(RequestTraceFilter::IsValidRequestId(missing));
            EXPECT_TRUE(RequestTraceFilter::IsValidRequestId(unsafe));
            EXPECT_NE(missing, unsafe);
            EXPECT_NE(unsafe, "trace id");
        }

        TEST(RequestTraceFilterTest, FilterPreservesExistingRequestAttribute) {
            auto request = RequestWithId("upstream-id");
            request->attributes()->insert("request_id", std::string("existing-id"));

            const auto response = drogon::sync_wait(RequestTraceFilter{}.doFilter(request));

            EXPECT_EQ(response, nullptr);
            EXPECT_EQ(
                request->attributes()->get<std::string>("request_id"),
                "existing-id"
            );
        }

        TEST(RequestTraceFilterTest, FilterStoresResolvedCallerId) {
            auto request = RequestWithId("upstream-id");

            const auto response = drogon::sync_wait(RequestTraceFilter{}.doFilter(request));

            EXPECT_EQ(response, nullptr);
            EXPECT_EQ(
                request->attributes()->get<std::string>("request_id"),
                "upstream-id"
            );
        }
    } // namespace
} // namespace disk::filters
