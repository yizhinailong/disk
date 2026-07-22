/**
 * @file ShareRateLimitLogContext_test.cpp
 * @brief Share rate-limit request correlation contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>
#include <json/json.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "filters/ShareAuthFilter.hpp"
#include "filters/ShareRateLimitFilter.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/LogHelper.hpp"

namespace disk::filters {
    namespace {

        constexpr std::string_view RAW_SHARE_TOKEN = "raw-replayable-share-token";
        constexpr std::string_view VERIFIED_JTI = "verified-rate-limit-jti";

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

        auto CountOccurrences(const std::string& source, std::string_view expected) -> size_t {
            size_t count = 0;
            size_t position = 0;
            while ((position = source.find(expected, position)) != std::string::npos) {
                ++count;
                position += expected.size();
            }
            return count;
        }

        auto MakeCountCounter(int64_t count) -> ShareRateLimitCounter {
            return [count](const std::string&, int) -> drogon::Task<Result<int64_t>> {
                co_return count;
            };
        }

        auto MakeFailureCounter() -> ShareRateLimitCounter {
            return [](const std::string&, int) -> drogon::Task<Result<int64_t>> {
                co_return std::unexpected(
                    ErrorInfo(
                        disk::error::Code::RedisOperationFailed,
                        "injected rate context failure"
                    )
                );
            };
        }

        auto CreateRequest(
            std::string path,
            std::string request_id = {}
        ) -> drogon::HttpRequestPtr {
            auto request = drogon::HttpRequest::newHttpRequest();
            request->setPath(std::move(path));
            if (!request_id.empty()) {
                request->attributes()->insert("request_id", std::move(request_id));
            }
            return request;
        }

        auto CreateOperationRequest(
            std::string path,
            std::string request_id,
            std::string jti = std::string(VERIFIED_JTI)
        ) -> drogon::HttpRequestPtr {
            auto request = CreateRequest(std::move(path), std::move(request_id));
            request->addHeader("X-Share-Token", std::string(RAW_SHARE_TOKEN));
            request->attributes()->insert(
                ShareAuthFilter::SHARE_TOKEN_JTI_ATTRIBUTE,
                std::move(jti)
            );
            return request;
        }

        class ShareRateLimitLogContextTest : public ::testing::Test {
        protected:
            auto SetUp() -> void override {
                m_previous_logger = spdlog::default_logger();
                m_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(m_output);
                m_logger = std::make_shared<spdlog::logger>(
                    "share-rate-limit-context-test",
                    m_sink
                );
                m_logger->set_level(spdlog::level::debug);
                disk::utils::Logger::ApplyStructuredFormatter(m_logger);
                spdlog::set_default_logger(m_logger);
                disk::utils::Logger::SetInstanceId("share-rate-limit-instance");
            }

            auto TearDown() -> void override {
                disk::utils::Logger::SetInstanceId("");
                spdlog::set_default_logger(m_previous_logger);
            }

            [[nodiscard]] auto DrainRecord() -> Json::Value {
                m_logger->flush();

                std::vector<Json::Value> records;
                std::istringstream lines(m_output.str());
                std::string line;
                while (std::getline(lines, line)) {
                    if (line.empty()) {
                        continue;
                    }

                    Json::CharReaderBuilder builder;
                    builder["collectComments"] = false;
                    const auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
                    Json::Value record;
                    std::string errors;
                    if (!reader->parse(
                            line.data(),
                            line.data() + line.size(),
                            &record,
                            &errors
                        )) {
                        ADD_FAILURE() << "Invalid structured log line: " << errors;
                        continue;
                    }
                    records.push_back(std::move(record));
                }

                m_output.str("");
                m_output.clear();
                EXPECT_EQ(records.size(), 1U);
                return records.empty() ? Json::Value{} : std::move(records.front());
            }

            static auto ExpectContext(
                const Json::Value& record,
                const char* request_id,
                std::string_view operation,
                std::string_view level,
                std::string_view message_marker
            ) -> void {
                if (request_id == nullptr) {
                    EXPECT_TRUE(record["request_id"].isNull());
                } else {
                    EXPECT_EQ(record["request_id"].asString(), request_id);
                }
                EXPECT_EQ(record["instance_id"].asString(), "share-rate-limit-instance");
                EXPECT_EQ(record["operation"].asString(), operation);
                EXPECT_EQ(record["level"].asString(), level);
                EXPECT_TRUE(Contains(record["message"].asString(), message_marker));
                for (const auto* field : {
                         "upload_id",
                         "job_id",
                         "lease_owner",
                         "state_version",
                     }) {
                    EXPECT_TRUE(record[field].isNull()) << field;
                }
                EXPECT_FALSE(Contains(record["message"].asString(), RAW_SHARE_TOKEN));
                EXPECT_FALSE(Contains(record["message"].asString(), VERIFIED_JTI));
            }

            std::shared_ptr<spdlog::logger> m_previous_logger;
            std::ostringstream m_output;
            std::shared_ptr<spdlog::sinks::ostream_sink_mt> m_sink;
            std::shared_ptr<spdlog::logger> m_logger;
        };

        TEST(ShareRateLimitLogContextContractTest, EveryDirectEventUsesRequestOwnedContext) {
            const auto source = ReadSourceFile("src/filters/ShareRateLimitFilter.cpp");

            ASSERT_FALSE(source.empty());
            EXPECT_EQ(CountOccurrences(source, "GetFilterLogContext(request)"), 2U);
            EXPECT_EQ(CountOccurrences(source, "Logger::Error(log_context)"), 4U);
            EXPECT_EQ(CountOccurrences(source, "Logger::Warn(log_context)"), 2U);
            EXPECT_FALSE(Contains(source, "Logger::Error()"));
            EXPECT_FALSE(Contains(source, "Logger::Warn()"));
            EXPECT_FALSE(Contains(source, "getHeader("));
            EXPECT_FALSE(Contains(source, "<< jti"));
            for (const auto* forbidden : {
                     "log_context.upload_id",
                     "log_context.job_id",
                     "log_context.lease_owner",
                     "log_context.state_version",
                 }) {
                EXPECT_FALSE(Contains(source, forbidden)) << forbidden;
            }
        }

        TEST_F(ShareRateLimitLogContextTest, AllFailureAndRejectionEventsPreserveBoundedContext) {
            ShareAccessRateLimitFilter access_failure_filter(MakeFailureCounter());
            EXPECT_EQ(
                drogon::sync_wait(access_failure_filter.doFilter(
                    CreateRequest("/api/share/access/share-code", "access-failure-request")
                )),
                nullptr
            );
            ExpectContext(
                DrainRecord(),
                "access-failure-request",
                "share",
                "error",
                "counter failed: operation=access"
            );

            ShareAccessRateLimitFilter access_rejection_filter(
                MakeCountCounter(std::numeric_limits<int64_t>::max())
            );
            const auto access_rejection = drogon::sync_wait(
                access_rejection_filter.doFilter(
                    CreateRequest("/api/share/access/share-code", "access-rejection-request")
                )
            );
            ASSERT_NE(access_rejection, nullptr);
            EXPECT_EQ(access_rejection->getStatusCode(), drogon::k429TooManyRequests);
            ExpectContext(
                DrainRecord(),
                "access-rejection-request",
                "share",
                "warning",
                "rate limit exceeded: operation=access"
            );

            ShareOperationRateLimitFilter missing_attribute_filter(MakeCountCounter(1));
            auto missing_attribute_request = CreateRequest("/api/share/browse/share-code");
            missing_attribute_request->addHeader(
                "X-Share-Token",
                std::string(RAW_SHARE_TOKEN)
            );
            EXPECT_EQ(
                drogon::sync_wait(
                    missing_attribute_filter.doFilter(missing_attribute_request)
                ),
                nullptr
            );
            ExpectContext(
                DrainRecord(),
                nullptr,
                "share",
                "error",
                "attribute missing: operation=browse"
            );

            ShareOperationRateLimitFilter empty_attribute_filter(MakeCountCounter(1));
            EXPECT_EQ(
                drogon::sync_wait(empty_attribute_filter.doFilter(
                    CreateOperationRequest(
                        "/api/share/browse/share-code",
                        "empty-attribute-request",
                        ""
                    )
                )),
                nullptr
            );
            ExpectContext(
                DrainRecord(),
                "empty-attribute-request",
                "share",
                "error",
                "attribute empty: operation=browse"
            );

            ShareOperationRateLimitFilter download_failure_filter(MakeFailureCounter());
            EXPECT_EQ(
                drogon::sync_wait(download_failure_filter.doFilter(
                    CreateOperationRequest(
                        "/api/share/download/share-code/7/info",
                        "download-failure-request"
                    )
                )),
                nullptr
            );
            ExpectContext(
                DrainRecord(),
                "download-failure-request",
                "download",
                "error",
                "counter failed: operation=download"
            );

            ShareOperationRateLimitFilter save_rejection_filter(
                MakeCountCounter(std::numeric_limits<int64_t>::max())
            );
            const auto save_rejection = drogon::sync_wait(
                save_rejection_filter.doFilter(
                    CreateOperationRequest(
                        "/api/share/save/share-code",
                        "save-rejection-request"
                    )
                )
            );
            ASSERT_NE(save_rejection, nullptr);
            EXPECT_EQ(save_rejection->getStatusCode(), drogon::k429TooManyRequests);
            ExpectContext(
                DrainRecord(),
                "save-rejection-request",
                "share",
                "warning",
                "rate limit exceeded: operation=download"
            );
        }

    } // namespace
} // namespace disk::filters
