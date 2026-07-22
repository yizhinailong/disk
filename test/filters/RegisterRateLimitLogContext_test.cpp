/**
 * @file RegisterRateLimitLogContext_test.cpp
 * @brief Register rate-limit request correlation contract tests
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

#include "filters/RegisterRateLimitFilter.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/LogHelper.hpp"

namespace disk::filters {
    namespace {

        constexpr std::string_view RAW_REGISTRATION_BODY =
            R"({"username":"register-log-user","email":"register-log@example.com","password":"raw-register-password-should-not-log"})";
        constexpr std::string_view RAW_REGISTER_PASSWORD =
            "raw-register-password-should-not-log";

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

        auto MakeCountCounter(int64_t count) -> RegisterRateLimitCounter {
            return [count](const std::string&, int) -> drogon::Task<Result<int64_t>> {
                co_return count;
            };
        }

        auto MakeFailureCounter() -> RegisterRateLimitCounter {
            return [](const std::string&, int) -> drogon::Task<Result<int64_t>> {
                co_return std::unexpected(
                    ErrorInfo(
                        disk::error::Code::RedisOperationFailed,
                        "injected register rate context failure"
                    )
                );
            };
        }

        auto CreateRequest(std::string request_id = {}) -> drogon::HttpRequestPtr {
            auto request = drogon::HttpRequest::newHttpRequest();
            request->setPath("/api/auth/register");
            request->setMethod(drogon::Post);
            request->setBody(std::string(RAW_REGISTRATION_BODY));
            if (!request_id.empty()) {
                request->attributes()->insert("request_id", std::move(request_id));
            }
            return request;
        }

        class RegisterRateLimitLogContextTest : public ::testing::Test {
        protected:
            auto SetUp() -> void override {
                m_previous_logger = spdlog::default_logger();
                m_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(m_output);
                m_logger = std::make_shared<spdlog::logger>(
                    "register-rate-limit-context-test",
                    m_sink
                );
                m_logger->set_level(spdlog::level::debug);
                disk::utils::Logger::ApplyStructuredFormatter(m_logger);
                spdlog::set_default_logger(m_logger);
                disk::utils::Logger::SetInstanceId("register-rate-limit-instance");
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
                std::string_view level,
                std::string_view message_marker
            ) -> void {
                if (request_id == nullptr) {
                    EXPECT_TRUE(record["request_id"].isNull());
                } else {
                    EXPECT_EQ(record["request_id"].asString(), request_id);
                }
                EXPECT_EQ(record["instance_id"].asString(), "register-rate-limit-instance");
                EXPECT_EQ(record["operation"].asString(), "auth");
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
                EXPECT_FALSE(Contains(record["message"].asString(), RAW_REGISTRATION_BODY));
                EXPECT_FALSE(Contains(record["message"].asString(), RAW_REGISTER_PASSWORD));
            }

            std::shared_ptr<spdlog::logger> m_previous_logger;
            std::ostringstream m_output;
            std::shared_ptr<spdlog::sinks::ostream_sink_mt> m_sink;
            std::shared_ptr<spdlog::logger> m_logger;
        };

        TEST(RegisterRateLimitLogContextContractTest, EveryDirectEventUsesRequestOwnedContext) {
            const auto source = ReadSourceFile("src/filters/RegisterRateLimitFilter.cpp");

            ASSERT_FALSE(source.empty());
            EXPECT_EQ(CountOccurrences(source, "GetFilterLogContext(request)"), 1U);
            EXPECT_EQ(CountOccurrences(source, "Logger::Error(log_context)"), 1U);
            EXPECT_EQ(CountOccurrences(source, "Logger::Warn(log_context)"), 1U);
            EXPECT_EQ(CountOccurrences(source, "Logger::Debug(log_context)"), 1U);
            for (const auto* level : { "Error", "Warn", "Debug" }) {
                EXPECT_FALSE(Contains(source, std::string("Logger::") + level + "()")) << level;
            }
            EXPECT_FALSE(Contains(source, "getHeader("));
            EXPECT_FALSE(Contains(source, "getJsonObject("));
            EXPECT_FALSE(Contains(source, "request->body("));
            EXPECT_TRUE(Contains(source, "CheckFixedWindowLimit(redis_service"));
            for (const auto* forbidden : {
                     "log_context.upload_id",
                     "log_context.job_id",
                     "log_context.lease_owner",
                     "log_context.state_version",
                 }) {
                EXPECT_FALSE(Contains(source, forbidden)) << forbidden;
            }
        }

        TEST_F(RegisterRateLimitLogContextTest, AllDirectEventsPreserveBoundedContext) {
            RegisterRateLimitFilter failure_filter(MakeFailureCounter());
            EXPECT_EQ(
                drogon::sync_wait(failure_filter.doFilter(
                    CreateRequest("register-rate-failure-request")
                )),
                nullptr
            );
            ExpectContext(
                DrainRecord(),
                "register-rate-failure-request",
                "error",
                "Redis IncrWithExpire failed:"
            );

            RegisterRateLimitFilter success_filter(MakeCountCounter(1));
            EXPECT_EQ(
                drogon::sync_wait(success_filter.doFilter(CreateRequest())),
                nullptr
            );
            ExpectContext(
                DrainRecord(),
                nullptr,
                "debug",
                "Register rate limit check passed:"
            );

            RegisterRateLimitFilter rejection_filter(
                MakeCountCounter(std::numeric_limits<int64_t>::max())
            );
            const auto rejection = drogon::sync_wait(
                rejection_filter.doFilter(CreateRequest("register-rate-rejection-request"))
            );
            ASSERT_NE(rejection, nullptr);
            EXPECT_EQ(rejection->getStatusCode(), drogon::k429TooManyRequests);
            ExpectContext(
                DrainRecord(),
                "register-rate-rejection-request",
                "warning",
                "Register rate limit:"
            );
        }

    } // namespace
} // namespace disk::filters
