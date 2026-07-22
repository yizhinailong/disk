/**
 * @file AuthFilterLogContext_test.cpp
 * @brief Authentication filter request correlation contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <drogon/HttpFilter.h>
#include <drogon/HttpRequest.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>
#include <json/json.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "filters/AdminAuthFilter.hpp"
#include "filters/JwtAuthFilter.hpp"
#include "filters/ShareAuthFilter.hpp"
#include "utils/LogHelper.hpp"

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

        auto CountOccurrences(const std::string& source, std::string_view expected) -> size_t {
            size_t count = 0;
            size_t position = 0;
            while ((position = source.find(expected, position)) != std::string::npos) {
                ++count;
                position += expected.size();
            }
            return count;
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

        class AuthFilterLogContextTest : public ::testing::Test {
        protected:
            auto SetUp() -> void override {
                m_previous_logger = spdlog::default_logger();
                m_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(m_output);
                m_logger = std::make_shared<spdlog::logger>("auth-filter-context-test", m_sink);
                m_logger->set_level(spdlog::level::debug);
                disk::utils::Logger::ApplyStructuredFormatter(m_logger);
                spdlog::set_default_logger(m_logger);
                disk::utils::Logger::SetInstanceId("auth-filter-instance");
            }

            auto TearDown() -> void override {
                disk::utils::Logger::SetInstanceId("");
                spdlog::set_default_logger(m_previous_logger);
            }

            [[nodiscard]] auto DrainRecords() -> std::vector<Json::Value> {
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
                return records;
            }

            static auto ExpectRequestContext(
                const Json::Value& record,
                std::string_view request_id,
                std::string_view operation,
                std::string_view message_marker
            ) -> void {
                EXPECT_EQ(record["request_id"].asString(), request_id);
                EXPECT_EQ(record["instance_id"].asString(), "auth-filter-instance");
                EXPECT_EQ(record["operation"].asString(), operation);
                EXPECT_TRUE(Contains(record["message"].asString(), message_marker));
                for (const auto* field : {
                         "upload_id",
                         "job_id",
                         "lease_owner",
                         "state_version",
                     }) {
                    EXPECT_TRUE(record[field].isNull()) << field;
                }
            }

            std::shared_ptr<spdlog::logger> m_previous_logger;
            std::ostringstream m_output;
            std::shared_ptr<spdlog::sinks::ostream_sink_mt> m_sink;
            std::shared_ptr<spdlog::logger> m_logger;
        };

        TEST(AuthFilterLogContextContractTest, EveryDirectEventUsesRequestOwnedContext) {
            const auto helper_source = ReadSourceFile("src/filters/FilterLogContext.hpp");
            const auto jwt_source = ReadSourceFile("src/filters/JwtAuthFilter.cpp");
            const auto share_source = ReadSourceFile("src/filters/ShareAuthFilter.cpp");
            const auto admin_source = ReadSourceFile("src/filters/AdminAuthFilter.cpp");

            ASSERT_FALSE(helper_source.empty());
            ASSERT_FALSE(jwt_source.empty());
            ASSERT_FALSE(share_source.empty());
            ASSERT_FALSE(admin_source.empty());

            EXPECT_TRUE(Contains(helper_source, "attributes->find(\"request_id\")"));
            EXPECT_TRUE(Contains(helper_source, "ClassifyHttpOperation(request->path())"));
            EXPECT_TRUE(Contains(helper_source, "HttpOperationName("));
            for (const auto* forbidden : {
                     "Authorization",
                     "X-Share-Token",
                     "user_id",
                     "share_code",
                     "jti",
                     "context.upload_id",
                     "context.job_id",
                     "context.lease_owner",
                     "context.state_version",
                 }) {
                EXPECT_FALSE(Contains(helper_source, forbidden)) << forbidden;
            }

            for (const auto* source : { &jwt_source, &share_source, &admin_source }) {
                EXPECT_EQ(CountOccurrences(*source, "GetFilterLogContext(request)"), 1U);
                for (const auto* level : { "Trace", "Debug", "Info", "Warn", "Error", "Fatal" }) {
                    EXPECT_FALSE(Contains(*source, std::string("Logger::") + level + "()"))
                        << level;
                }
            }

            EXPECT_EQ(CountOccurrences(jwt_source, "Logger::Info(log_context)"), 6U);
            EXPECT_EQ(CountOccurrences(jwt_source, "Logger::Warn(log_context)"), 1U);
            EXPECT_EQ(CountOccurrences(jwt_source, "Logger::Error(log_context)"), 1U);
            EXPECT_EQ(CountOccurrences(share_source, "Logger::Info(log_context)"), 3U);
            EXPECT_EQ(CountOccurrences(share_source, "Logger::Warn(log_context)"), 1U);
            EXPECT_EQ(CountOccurrences(admin_source, "Logger::Warn(log_context)"), 2U);
            EXPECT_EQ(CountOccurrences(admin_source, "Logger::Trace(log_context)"), 1U);
        }

        TEST_F(AuthFilterLogContextTest, RejectionsPreserveBoundedContextWithoutOwnershipInference) {
            auto jwt_request = CreateRequest("/api/file/list", "jwt-filter-request");
            const auto jwt_response = drogon::sync_wait(JwtAuthFilter{}.doFilter(jwt_request));
            ASSERT_NE(jwt_response, nullptr);
            EXPECT_EQ(jwt_response->getStatusCode(), drogon::k401Unauthorized);
            const auto jwt_records = DrainRecords();
            ASSERT_EQ(jwt_records.size(), 1U);
            ExpectRequestContext(
                jwt_records.front(),
                "jwt-filter-request",
                "file_query",
                "[jwt_auth_filter]"
            );

            auto share_request = CreateRequest(
                "/api/share/browse/runtime-share",
                "share-filter-request"
            );
            const auto share_response = drogon::sync_wait(
                ShareAuthFilter{}.doFilter(share_request)
            );
            ASSERT_NE(share_response, nullptr);
            EXPECT_EQ(share_response->getStatusCode(), drogon::k401Unauthorized);
            const auto share_records = DrainRecords();
            ASSERT_EQ(share_records.size(), 1U);
            ExpectRequestContext(
                share_records.front(),
                "share-filter-request",
                "share",
                "[share_auth_filter]"
            );

            auto admin_request = CreateRequest("/api/admin/users", "admin-filter-request");
            admin_request->attributes()->insert("user_id", uint64_t{ 42 });
            admin_request->attributes()->insert("role", 0);
            admin_request->attributes()->insert("status", 1);
            const auto admin_response = drogon::sync_wait(
                AdminAuthFilter{}.doFilter(admin_request)
            );
            ASSERT_NE(admin_response, nullptr);
            EXPECT_EQ(admin_response->getStatusCode(), drogon::k403Forbidden);
            const auto admin_records = DrainRecords();
            ASSERT_EQ(admin_records.size(), 1U);
            ExpectRequestContext(
                admin_records.front(),
                "admin-filter-request",
                "admin",
                "[admin_auth_filter]"
            );

            auto missing_request_id = CreateRequest("/api/user/profile");
            const auto missing_response = drogon::sync_wait(
                JwtAuthFilter{}.doFilter(missing_request_id)
            );
            ASSERT_NE(missing_response, nullptr);
            EXPECT_EQ(missing_response->getStatusCode(), drogon::k401Unauthorized);
            const auto missing_records = DrainRecords();
            ASSERT_EQ(missing_records.size(), 1U);
            EXPECT_TRUE(missing_records.front()["request_id"].isNull());
            ExpectRequestContext(
                missing_records.front(),
                "",
                "user",
                "[jwt_auth_filter]"
            );
        }

    } // namespace
} // namespace disk::filters
