/**
 * @file RateLimitLogContext_test.cpp
 * @brief Trash user rate-limit request correlation contract tests
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

#include "filters/RateLimitFilter.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/LogHelper.hpp"

namespace disk::filters {
    namespace {

        constexpr std::string_view RAW_ACCESS_TOKEN = "raw-trash-access-token";
        constexpr std::string_view RAW_TRASH_ID = "987654321";
        constexpr std::string_view RAW_DELETE_BODY = R"({"trash_ids":[987654321]})";

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

        auto MakeCountCounter(int64_t count) -> ApiRateLimitCounter {
            return [count](const std::string&, int, disk::utils::LogContext)
                       -> drogon::Task<Result<int64_t>> {
                co_return count;
            };
        }

        auto MakeFailureCounter() -> ApiRateLimitCounter {
            return [](const std::string&, int, disk::utils::LogContext)
                       -> drogon::Task<Result<int64_t>> {
                co_return std::unexpected(
                    ErrorInfo(
                        disk::error::Code::RedisOperationFailed,
                        "injected trash rate context failure"
                    )
                );
            };
        }

        auto CreateRequest(
            std::string path,
            std::string request_id = {},
            bool include_user = true
        ) -> drogon::HttpRequestPtr {
            auto request = drogon::HttpRequest::newHttpRequest();
            request->setPath(std::move(path));
            request->setMethod(drogon::Delete);
            request->addHeader("Authorization", "Bearer " + std::string(RAW_ACCESS_TOKEN));
            request->setBody(std::string(RAW_DELETE_BODY));
            if (include_user) {
                request->attributes()->insert("user_id", uint64_t{ 42 });
            }
            if (!request_id.empty()) {
                request->attributes()->insert("request_id", std::move(request_id));
            }
            return request;
        }

        class RateLimitLogContextTest : public ::testing::Test {
        protected:
            auto SetUp() -> void override {
                m_previous_logger = spdlog::default_logger();
                m_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(m_output);
                m_logger = std::make_shared<spdlog::logger>(
                    "trash-rate-limit-context-test",
                    m_sink
                );
                m_logger->set_level(spdlog::level::debug);
                disk::utils::Logger::ApplyStructuredFormatter(m_logger);
                spdlog::set_default_logger(m_logger);
                disk::utils::Logger::SetInstanceId("trash-rate-limit-instance");
            }

            auto TearDown() -> void override {
                disk::utils::Logger::SetInstanceId("");
                spdlog::set_default_logger(m_previous_logger);
            }

            [[nodiscard]] auto DrainRecords(size_t expected_count) -> std::vector<Json::Value> {
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
                EXPECT_EQ(records.size(), expected_count);
                return records;
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
                EXPECT_EQ(record["instance_id"].asString(), "trash-rate-limit-instance");
                EXPECT_EQ(record["operation"].asString(), "trash");
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
                for (const auto sensitive_value : {
                         RAW_ACCESS_TOKEN,
                         RAW_TRASH_ID,
                         RAW_DELETE_BODY,
                     }) {
                    EXPECT_FALSE(Contains(record["message"].asString(), sensitive_value));
                }
            }

            std::shared_ptr<spdlog::logger> m_previous_logger;
            std::ostringstream m_output;
            std::shared_ptr<spdlog::sinks::ostream_sink_mt> m_sink;
            std::shared_ptr<spdlog::logger> m_logger;
        };

        TEST(RateLimitLogContextContractTest, EveryDirectEventUsesRequestOwnedContext) {
            const auto source = ReadSourceFile("src/filters/RateLimitFilter.cpp");

            ASSERT_FALSE(source.empty());
            EXPECT_EQ(RateLimitFilter::DEFAULT_LIMIT, 100);
            EXPECT_EQ(RateLimitFilter::WINDOW_SECONDS, 60);
            EXPECT_EQ(CountOccurrences(source, "GetFilterLogContext(request)"), 1U);
            EXPECT_EQ(CountOccurrences(source, "Logger::Error(log_context)"), 1U);
            EXPECT_EQ(CountOccurrences(source, "Logger::Warn(log_context)"), 2U);
            EXPECT_EQ(CountOccurrences(source, "Logger::Info(log_context)"), 2U);
            EXPECT_EQ(CountOccurrences(source, "Logger::Debug(log_context)"), 1U);
            for (const auto* level : { "Error", "Warn", "Info", "Debug" }) {
                EXPECT_FALSE(Contains(source, std::string("Logger::") + level + "()")) << level;
            }
            EXPECT_FALSE(Contains(source, "getHeader("));
            EXPECT_FALSE(Contains(source, "getJsonObject("));
            EXPECT_FALSE(Contains(source, "request->body("));
            EXPECT_TRUE(Contains(source, "CheckFixedWindowLimit(redis_service"));
            EXPECT_TRUE(Contains(source, "RedisService::Initialize(drogon::app().getRedisClient())"));
            EXPECT_TRUE(Contains(source, "RedisKeyPrefix::BuildApiRateLimitKey"));
            EXPECT_TRUE(Contains(source, "BuildRateLimitExceededResponse("));
            EXPECT_TRUE(Contains(source, "false"));
            for (const auto* forbidden : {
                     "log_context.upload_id",
                     "log_context.job_id",
                     "log_context.lease_owner",
                     "log_context.state_version",
                 }) {
                EXPECT_FALSE(Contains(source, forbidden)) << forbidden;
            }
        }

        TEST_F(RateLimitLogContextTest, TrashRoutesPreserveContextAndResponseContract) {
            RateLimitFilter missing_user_filter(MakeCountCounter(1));
            EXPECT_EQ(
                drogon::sync_wait(missing_user_filter.doFilter(CreateRequest(
                    "/api/trash/delete",
                    "trash-rate-missing-user",
                    false
                ))),
                nullptr
            );
            EXPECT_TRUE(DrainRecords(0).empty());

            RateLimitFilter failure_filter(MakeFailureCounter());
            EXPECT_EQ(
                drogon::sync_wait(failure_filter.doFilter(CreateRequest(
                    "/api/trash",
                    "trash-rate-failure"
                ))),
                nullptr
            );
            auto failure_records = DrainRecords(1);
            ASSERT_EQ(failure_records.size(), 1U);
            ExpectContext(
                failure_records[0],
                "trash-rate-failure",
                "error",
                "Redis IncrWithExpire failed:"
            );

            RateLimitFilter success_filter(MakeCountCounter(1));
            EXPECT_EQ(
                drogon::sync_wait(success_filter.doFilter(CreateRequest(
                    "/api/trash/restore"
                ))),
                nullptr
            );
            auto success_records = DrainRecords(2);
            ASSERT_EQ(success_records.size(), 2U);
            ExpectContext(
                success_records[0],
                nullptr,
                "debug",
                "API rate limit check passed:"
            );
            ExpectContext(
                success_records[1],
                nullptr,
                "info",
                "outcome=success"
            );

            RateLimitFilter rejection_filter(
                MakeCountCounter(std::numeric_limits<int64_t>::max())
            );
            const auto rejection = drogon::sync_wait(
                rejection_filter.doFilter(CreateRequest(
                    "/api/trash/all",
                    "trash-rate-rejection"
                ))
            );
            ASSERT_NE(rejection, nullptr);
            EXPECT_EQ(rejection->getStatusCode(), drogon::k429TooManyRequests);
            EXPECT_EQ(rejection->getHeader("X-RateLimit-Limit"), "100");
            EXPECT_EQ(rejection->getHeader("X-RateLimit-Remaining"), "0");
            EXPECT_FALSE(rejection->getHeader("X-RateLimit-Reset").empty());
            EXPECT_TRUE(rejection->getHeader("Retry-After").empty());

            auto rejection_records = DrainRecords(2);
            ASSERT_EQ(rejection_records.size(), 2U);
            ExpectContext(
                rejection_records[0],
                "trash-rate-rejection",
                "warning",
                "API rate limit:"
            );
            ExpectContext(
                rejection_records[1],
                "trash-rate-rejection",
                "info",
                "outcome=failure"
            );
        }

    } // namespace
} // namespace disk::filters
