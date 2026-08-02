/**
 * @file RequestTraceFilter_test.cpp
 * @brief Request ID validation and propagation tests
 */

#include "filters/RequestTraceFilter.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include <drogon/HttpFilter.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>

namespace disk::filters {
    namespace {
        auto RepositoryRoot() -> std::filesystem::path {
            return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
        }

        auto ReadSourceFile(const std::filesystem::path& relative_path) -> std::string {
            std::ifstream input(RepositoryRoot() / relative_path);
            std::ostringstream buffer;
            buffer << input.rdbuf();
            return buffer.str();
        }

        auto Contains(const std::string& source, std::string_view expected) -> bool {
            return source.find(expected) != std::string::npos;
        }

        template <typename Filter>
        concept HasRequestIdGenerator = requires {
            Filter::GenerateRequestId();
        };

        template <typename Filter>
        concept HasRequestIdValidator = requires(std::string_view request_id) {
            Filter::IsValidRequestId(request_id);
        };

        template <typename Filter>
        concept HasRequestIdResolver = requires(const drogon::HttpRequestPtr& request) {
            Filter::ResolveRequestId(request);
        };

        static_assert(!HasRequestIdGenerator<RequestTraceFilter>);
        static_assert(!HasRequestIdValidator<RequestTraceFilter>);
        static_assert(HasRequestIdResolver<RequestTraceFilter>);

        auto RequestWithId(std::string_view request_id) -> drogon::HttpRequestPtr {
            auto request = drogon::HttpRequest::newHttpRequest();
            if (!request_id.empty()) {
                request->addHeader("X-Request-Id", std::string(request_id));
            }
            return request;
        }

        auto IsLowerHex(char character) -> bool {
            return (character >= '0' && character <= '9') ||
                   (character >= 'a' && character <= 'f');
        }

        auto IsUuidV4(std::string_view request_id) -> bool {
            if (request_id.size() != 36 || request_id[8] != '-' ||
                request_id[13] != '-' || request_id[18] != '-' ||
                request_id[23] != '-') {
                return false;
            }
            for (std::size_t index = 0; index < request_id.size(); ++index) {
                if (index == 8 || index == 13 || index == 18 || index == 23) {
                    continue;
                }
                if (!IsLowerHex(request_id[index])) {
                    return false;
                }
            }
            const auto variant = request_id[19];
            return request_id[14] == '4' &&
                   (variant == '8' || variant == '9' || variant == 'a' || variant == 'b');
        }

        TEST(RequestTraceFilterTest, AcceptsSafeCallerRequestIdsAtLengthBoundary) {
            const std::string maximum_length_id(128, 'a');

            EXPECT_EQ(
                RequestTraceFilter::ResolveRequestId(
                    RequestWithId("trace.API_01:attempt-2")
                ),
                "trace.API_01:attempt-2"
            );
            EXPECT_EQ(
                RequestTraceFilter::ResolveRequestId(RequestWithId(maximum_length_id)),
                maximum_length_id
            );
        }

        TEST(RequestTraceFilterTest, RejectsEmptyOversizedAndUnsafeCallerRequestIds) {
            for (const auto& unsafe_request_id : {
                     std::string(),
                     std::string(129, 'a'),
                     std::string("trace id"),
                     std::string("trace/id"),
                     std::string("trace\nforged"),
                     std::string("trace\rforged"),
                 }) {
                const auto resolved = RequestTraceFilter::ResolveRequestId(
                    RequestWithId(unsafe_request_id)
                );
                EXPECT_TRUE(IsUuidV4(resolved));
                EXPECT_NE(resolved, unsafe_request_id);
            }
        }

        TEST(RequestTraceFilterTest, MissingOrUnsafeCallerIdFallsBackToValidUuid) {
            const auto missing = RequestTraceFilter::ResolveRequestId(RequestWithId(""));
            const auto unsafe =
                RequestTraceFilter::ResolveRequestId(RequestWithId("trace id"));

            EXPECT_TRUE(IsUuidV4(missing));
            EXPECT_TRUE(IsUuidV4(unsafe));
            EXPECT_NE(missing, unsafe);
            EXPECT_NE(unsafe, "trace id");
        }

        TEST(RequestTraceFilterTest, UsesCryptographicSystemRandomBytesForUuidV4) {
            const auto source = ReadSourceFile("src/filters/RequestTraceFilter.cpp");

            EXPECT_FALSE(Contains(source, "#include <random>"));
            EXPECT_FALSE(Contains(source, "std::random_device"));
            EXPECT_FALSE(Contains(source, "std::mt19937"));
            EXPECT_FALSE(Contains(source, "std::uniform_int_distribution"));
            EXPECT_TRUE(Contains(source, "randombytes_buf(random_bytes.data(), random_bytes.size())"));
            EXPECT_TRUE(Contains(source, "random_bytes[6] ="));
            EXPECT_TRUE(Contains(source, "random_bytes[8] ="));
        }

        TEST(RequestTraceFilterTest, ConcurrentFallbackIdsAreValidAndUnique) {
            constexpr std::size_t THREAD_COUNT = 8;
            constexpr std::size_t IDS_PER_THREAD = 512;
            constexpr std::size_t TOTAL_IDS = THREAD_COUNT * IDS_PER_THREAD;

            std::vector<std::vector<std::string>> generated_ids(THREAD_COUNT);
            std::vector<std::thread> threads;
            threads.reserve(THREAD_COUNT);
            for (std::size_t thread_index = 0; thread_index < THREAD_COUNT; ++thread_index) {
                generated_ids[thread_index].reserve(IDS_PER_THREAD);
                threads.emplace_back([&, thread_index] {
                    for (std::size_t index = 0; index < IDS_PER_THREAD; ++index) {
                        generated_ids[thread_index].push_back(
                            RequestTraceFilter::ResolveRequestId(RequestWithId(""))
                        );
                    }
                });
            }
            for (auto& thread : threads) {
                thread.join();
            }

            std::unordered_set<std::string> unique_ids;
            unique_ids.reserve(TOTAL_IDS);
            for (const auto& thread_ids : generated_ids) {
                for (const auto& request_id : thread_ids) {
                    EXPECT_TRUE(IsUuidV4(request_id)) << request_id;
                    EXPECT_TRUE(unique_ids.insert(request_id).second) << request_id;
                }
            }
            EXPECT_EQ(unique_ids.size(), TOTAL_IDS);
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
