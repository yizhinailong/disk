/**
 * @file RedisServiceLogContext_test.cpp
 * @brief Redis command correlation and redaction contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <drogon/nosql/RedisClient.h>
#include <drogon/utils/Utilities.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>
#include <json/json.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>
#include <trantor/net/InetAddress.h>

#include "services/RedisService.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/LogHelper.hpp"

namespace disk::services {
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

        auto StreamsIdentifier(const std::string& source, std::string_view identifier) -> bool {
            size_t position = 0;
            while ((position = source.find("<<", position)) != std::string::npos) {
                auto cursor = position + 2;
                while (cursor < source.size() &&
                       std::isspace(static_cast<unsigned char>(source[cursor])) != 0) {
                    ++cursor;
                }
                const auto end = cursor + identifier.size();
                if (end <= source.size() &&
                    source.compare(cursor, identifier.size(), identifier) == 0 &&
                    (end == source.size() ||
                     (std::isalnum(static_cast<unsigned char>(source[end])) == 0 &&
                      source[end] != '_'))) {
                    return true;
                }
                position = cursor;
            }
            return false;
        }

        auto EveryCallContainsContext(
            const std::string& source,
            std::string_view marker,
            size_t expected_count
        ) -> bool {
            size_t count = 0;
            size_t position = 0;
            while ((position = source.find(marker, position)) != std::string::npos) {
                const auto end = source.find(");", position);
                if (end == std::string::npos ||
                    !Contains(source.substr(position, end - position), "log_context")) {
                    return false;
                }
                ++count;
                position = end + 2;
            }
            return count == expected_count;
        }

        auto PingRedis(const drogon::nosql::RedisClientPtr& redis_client)
            -> drogon::Task<std::string> {
            auto result = co_await redis_client->execCommandCoro("PING");
            co_return result.asString();
        }

        auto WaitForRedisReady(const drogon::nosql::RedisClientPtr& redis_client) -> bool {
            for (int attempt = 0; attempt < 30; ++attempt) {
                try {
                    if (drogon::sync_wait(PingRedis(redis_client)) == "PONG") {
                        return true;
                    }
                } catch (const drogon::nosql::RedisException&) {
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            return false;
        }

        auto MakeContext(std::string request_id, std::string operation)
            -> disk::utils::LogContext {
            return {
                .request_id = std::move(request_id),
                .operation = std::move(operation),
            };
        }

        auto Serialize(const Json::Value& value) -> std::string {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            return Json::writeString(builder, value);
        }

        TEST(RedisServiceLogContextContractTest, CommandsAndProductionCallersUseExplicitContext) {
            const auto header = ReadSourceFile("src/services/RedisService.hpp");
            const auto source = ReadSourceFile("src/services/RedisService.cpp");
            const auto token = ReadSourceFile("src/services/TokenService.cpp");
            const auto auth = ReadSourceFile("src/services/AuthService.cpp");
            const auto share = ReadSourceFile("src/services/ShareService.cpp");
            const auto file_cache = ReadSourceFile("src/services/FileListCache.cpp");
            const auto file_query = ReadSourceFile("src/services/FileQueryService.cpp");
            const auto rate_helper = ReadSourceFile("src/filters/RateLimitHelper.hpp");
            const auto admin = ReadSourceFile("src/services/AdminService.cpp");
            const auto health = ReadSourceFile("src/services/HealthService.cpp");
            const auto health_controller = ReadSourceFile("src/controllers/HealthController.cpp");

            ASSERT_FALSE(header.empty());
            ASSERT_FALSE(source.empty());
            EXPECT_EQ(
                CountOccurrences(header, "disk::utils::LogContext log_context = {}"),
                13U
            );
            EXPECT_EQ(CountOccurrences(source, "Logger::Trace(log_context)"), 7U);
            EXPECT_EQ(CountOccurrences(source, "Logger::Error(log_context)"), 16U);
            EXPECT_EQ(CountOccurrences(source, "Logger::Debug()"), 1U);
            EXPECT_FALSE(Contains(source, "Logger::Trace()"));
            EXPECT_FALSE(Contains(source, "Logger::Error()"));
            EXPECT_FALSE(Contains(source, "Logger::Debug(log_context)"));
            EXPECT_TRUE(Contains(source, "RedisService initialized"));
            EXPECT_TRUE(Contains(source, "WrapRedisResultParseError(\"PING\", ex, log_context)"));

            for (const auto* sensitive : {
                     "key",
                     "value",
                     "expected",
                     "new_value",
                     "CAS_LUA_SCRIPT",
                     "RATE_LIMIT_LUA_SCRIPT",
                 }) {
                EXPECT_FALSE(StreamsIdentifier(source, sensitive)) << sensitive;
            }
            EXPECT_FALSE(Contains(source, "<< ex.what()"));

            EXPECT_TRUE(EveryCallContainsContext(token, "m_redis_service->Set(", 3U));
            EXPECT_TRUE(EveryCallContainsContext(token, "m_redis_service->CompareAndSwap(", 1U));
            EXPECT_TRUE(EveryCallContainsContext(token, "m_redis_service->Delete(", 1U));
            EXPECT_TRUE(EveryCallContainsContext(token, "m_redis_service->Exists(", 2U));
            EXPECT_TRUE(EveryCallContainsContext(auth, "m_redis_service->IncrWithExpire(", 1U));
            EXPECT_TRUE(EveryCallContainsContext(auth, "m_redis_service->Delete(", 1U));
            EXPECT_TRUE(EveryCallContainsContext(share, "m_redis_service->IncrWithExpire(", 1U));
            EXPECT_TRUE(EveryCallContainsContext(file_cache, "redis_service->Get(", 1U));
            EXPECT_TRUE(EveryCallContainsContext(file_cache, "redis_service->Incr(", 1U));
            EXPECT_TRUE(EveryCallContainsContext(file_query, "m_redis_service->Get(", 1U));
            EXPECT_TRUE(EveryCallContainsContext(file_query, "m_redis_service->Set(", 1U));
            EXPECT_TRUE(EveryCallContainsContext(rate_helper, "redis_service->IncrWithExpire(", 1U));
            EXPECT_TRUE(EveryCallContainsContext(admin, "RedisService::GetInstance()->Ping(", 1U));
            EXPECT_TRUE(EveryCallContainsContext(health, "RedisService::GetInstance()->Ping(", 1U));
            EXPECT_TRUE(Contains(health_controller, "CheckReadiness(log_context)"));
            EXPECT_EQ(
                CountOccurrences(
                    health_controller,
                    "GetRequestLogContext(request, \"health\")"
                ),
                2U
            );

            for (const auto* filter : {
                     "src/filters/RateLimitFilter.cpp",
                     "src/filters/RegisterRateLimitFilter.cpp",
                     "src/filters/UploadRateLimitFilter.cpp",
                     "src/filters/DownloadRateLimitFilter.cpp",
                     "src/filters/FolderRateLimitFilter.cpp",
                     "src/filters/AdminRateLimitFilter.cpp",
                 }) {
                const auto filter_source = ReadSourceFile(filter);
                EXPECT_TRUE(EveryCallContainsContext(
                    filter_source,
                    "co_await m_counter(",
                    1U
                )) << filter;
            }
            const auto share_filter = ReadSourceFile("src/filters/ShareRateLimitFilter.cpp");
            EXPECT_TRUE(EveryCallContainsContext(
                share_filter,
                "co_await m_counter(",
                2U
            ));
        }

        class RedisServiceLogContextTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                s_redis_client = drogon::nosql::RedisClient::newRedisClient(
                    trantor::InetAddress("127.0.0.1", 6379),
                    1
                );
                ASSERT_NE(s_redis_client, nullptr);
                ASSERT_TRUE(WaitForRedisReady(s_redis_client));
                RedisService::Initialize(s_redis_client);
            }

            auto SetUp() -> void override {
                m_service = RedisService::GetInstance();
                ASSERT_NE(m_service, nullptr);
                m_key_prefix = "test:redis_log_context:" + drogon::utils::getUuid();

                m_previous_logger = spdlog::default_logger();
                m_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(m_output);
                m_logger = std::make_shared<spdlog::logger>(
                    "redis-service-context-test",
                    m_sink
                );
                m_logger->set_level(spdlog::level::trace);
                disk::utils::Logger::ApplyStructuredFormatter(m_logger);
                spdlog::set_default_logger(m_logger);
                disk::utils::Logger::SetInstanceId("redis-service-instance");
            }

            auto TearDown() -> void override {
                for (const auto& key : m_tracked_keys) {
                    static_cast<void>(drogon::sync_wait(m_service->Delete(key)));
                }
                disk::utils::Logger::SetInstanceId("");
                spdlog::set_default_logger(m_previous_logger);
            }

            auto TrackKey(std::string_view suffix) -> std::string {
                auto key = m_key_prefix + ":" + std::string(suffix);
                m_tracked_keys.push_back(key);
                return key;
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
                const char* operation,
                std::string_view level,
                std::string_view message_marker
            ) -> void {
                if (request_id == nullptr) {
                    EXPECT_TRUE(record["request_id"].isNull());
                } else {
                    EXPECT_EQ(record["request_id"].asString(), request_id);
                }
                if (operation == nullptr) {
                    EXPECT_TRUE(record["operation"].isNull());
                } else {
                    EXPECT_EQ(record["operation"].asString(), operation);
                }
                EXPECT_EQ(record["instance_id"].asString(), "redis-service-instance");
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
            }

            static auto ExpectSecretsExcluded(
                const std::vector<Json::Value>& records,
                const std::vector<std::string>& secrets
            ) -> void {
                for (const auto& record : records) {
                    const auto serialized = Serialize(record);
                    for (const auto& secret : secrets) {
                        EXPECT_FALSE(secret.empty());
                        EXPECT_FALSE(Contains(serialized, secret)) << secret;
                    }
                }
            }

            inline static drogon::nosql::RedisClientPtr s_redis_client;

            std::shared_ptr<RedisService> m_service;
            std::string m_key_prefix;
            std::vector<std::string> m_tracked_keys;
            std::shared_ptr<spdlog::logger> m_previous_logger;
            std::ostringstream m_output;
            std::shared_ptr<spdlog::sinks::ostream_sink_mt> m_sink;
            std::shared_ptr<spdlog::logger> m_logger;
        };

        TEST_F(RedisServiceLogContextTest, SuccessfulCommandsPreserveContextWithoutSecrets) {
            const auto context = MakeContext("redis-success-request", "redis_contract");
            const auto exists_key = TrackKey("secret-exists-key");
            const auto missing_key = TrackKey("secret-missing-key");
            const auto counter_key = TrackKey("secret-counter-key");
            const auto cas_key = TrackKey("secret-cas-key");
            const auto rate_key = TrackKey("secret-rate-key");
            const std::string initial_value = "redis-secret-initial-value";
            const std::string expected_value = "redis-secret-expected-value";
            const std::string new_value = "redis-secret-new-value";

            ASSERT_TRUE(drogon::sync_wait(m_service->Set(exists_key, initial_value)).has_value());
            ASSERT_TRUE(drogon::sync_wait(m_service->Exists(exists_key, context)).value());
            ASSERT_TRUE(drogon::sync_wait(m_service->Expire(exists_key, 60, context)).has_value());
            const auto missing_expire =
                drogon::sync_wait(m_service->Expire(missing_key, 60, context));
            ASSERT_FALSE(missing_expire.has_value());
            EXPECT_EQ(missing_expire.error().code, ErrorCode::RedisKeyNotFound);
            ASSERT_EQ(drogon::sync_wait(m_service->Incr(counter_key, context)).value(), 1);
            ASSERT_EQ(drogon::sync_wait(m_service->IncrBy(counter_key, 4, context)).value(), 5);
            ASSERT_TRUE(drogon::sync_wait(m_service->Set(cas_key, expected_value)).has_value());
            ASSERT_TRUE(drogon::sync_wait(m_service->CompareAndSwap(cas_key, expected_value, new_value, 60, context)).value());
            ASSERT_EQ(drogon::sync_wait(m_service->IncrWithExpire(rate_key, 60, context)).value(), 1);

            const auto records = DrainRecords(7);
            ASSERT_EQ(records.size(), 7U);
            ExpectContext(records[0], "redis-success-request", "redis_contract", "trace", "Redis EXISTS:");
            ExpectContext(records[1], "redis-success-request", "redis_contract", "trace", "Redis EXPIRE:");
            ExpectContext(records[2], "redis-success-request", "redis_contract", "trace", "Redis EXPIRE:");
            ExpectContext(records[3], "redis-success-request", "redis_contract", "trace", "Redis INCR succeeded");
            ExpectContext(records[4], "redis-success-request", "redis_contract", "trace", "Redis INCRBY:");
            ExpectContext(records[5], "redis-success-request", "redis_contract", "trace", "Redis CAS:");
            ExpectContext(records[6], "redis-success-request", "redis_contract", "trace", "Redis IncrWithExpire succeeded:");
            ExpectSecretsExcluded(
                records,
                {
                    exists_key,
                    missing_key,
                    counter_key,
                    cas_key,
                    rate_key,
                    initial_value,
                    expected_value,
                    new_value,
                }
            );
        }

        TEST_F(RedisServiceLogContextTest, CommandFailurePreservesContextAndRedactsException) {
            const auto context = MakeContext("redis-error-request", "redis_contract");
            const auto key = TrackKey("secret-error-key");
            const std::string poison_value = "redis-secret-poison-value";
            ASSERT_TRUE(drogon::sync_wait(m_service->Set(key, poison_value)).has_value());

            const auto result = drogon::sync_wait(m_service->Incr(key, context));
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::RedisOperationFailed);

            const auto records = DrainRecords(1);
            ASSERT_EQ(records.size(), 1U);
            ExpectContext(
                records.front(),
                "redis-error-request",
                "redis_contract",
                "error",
                "Redis operation failed: INCR"
            );
            EXPECT_EQ(records.front()["message"].asString(), "Redis operation failed: INCR");
            EXPECT_FALSE(Contains(Serialize(records.front()), "not an integer"));
            ExpectSecretsExcluded(records, { key, poison_value });
        }

        TEST_F(RedisServiceLogContextTest, DefaultCallerKeepsNullRequestCorrelation) {
            const auto key = TrackKey("secret-default-key");
            const auto result = drogon::sync_wait(m_service->Exists(key));
            ASSERT_TRUE(result.has_value());
            EXPECT_FALSE(*result);

            const auto records = DrainRecords(1);
            ASSERT_EQ(records.size(), 1U);
            ExpectContext(records.front(), nullptr, nullptr, "trace", "Redis EXISTS:");
            ExpectSecretsExcluded(records, { key });
        }

    } // namespace
} // namespace disk::services
