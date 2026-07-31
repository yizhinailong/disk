#include "services/StorageWorkerRuntime.hpp"

#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <json/json.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "utils/LogHelper.hpp"

namespace disk::jobs {
    namespace {
        constexpr std::string_view RESULT_SECRET =
            "result-secret sql=SELECT endpoint=http://database.internal";
        constexpr std::string_view EXCEPTION_SECRET =
            "exception-secret credential=worker-password";

        template <typename T>
        concept HasIsStarted = requires(const T& runtime) {
            runtime.IsStarted();
        };

        template <typename T>
        concept HasIsAccepting = requires(const T& runtime) {
            runtime.IsAccepting();
        };

        static_assert(!HasIsStarted<StorageWorkerRuntime>);
        static_assert(!HasIsAccepting<StorageWorkerRuntime>);

        TEST(StorageWorkerRuntimeContractTest, ConstructorUsesOnlyRuntimeInputs) {
            EXPECT_TRUE((std::is_constructible_v<
                         StorageWorkerRuntime,
                         StorageWorkerRuntime::RunCallback,
                         StorageWorkerRuntimeOptions>));
            EXPECT_FALSE((std::is_constructible_v<
                          StorageWorkerRuntime,
                          std::string,
                          StorageWorkerRuntime::RunCallback,
                          StorageWorkerRuntimeOptions>));
        }

        class StorageWorkerRuntimeLogTest : public ::testing::Test {
        protected:
            auto SetUp() -> void override {
                m_previous_logger = spdlog::default_logger();
                m_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(m_output);
                m_logger = std::make_shared<spdlog::logger>(
                    "storage-worker-runtime-log-test",
                    m_sink
                );
                m_logger->set_level(spdlog::level::trace);
                utils::Logger::ApplyStructuredFormatter(m_logger);
                spdlog::set_default_logger(m_logger);
                utils::Logger::SetInstanceId("structured-runtime-instance");
            }

            auto TearDown() -> void override {
                utils::Logger::SetInstanceId("");
                spdlog::set_default_logger(m_previous_logger);
            }

            [[nodiscard]] auto Records() -> std::vector<Json::Value> {
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
                    const auto reader = std::unique_ptr<Json::CharReader>(
                        builder.newCharReader()
                    );
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
                return records;
            }

            static auto ExpectContext(
                const Json::Value& record,
                std::string_view operation,
                std::string_view level,
                std::string_view message
            ) -> void {
                EXPECT_EQ(record["schema_version"].asInt64(), 1);
                EXPECT_EQ(record["source"].asString(), "application");
                EXPECT_EQ(record["instance_id"].asString(), "structured-runtime-instance");
                EXPECT_EQ(record["operation"].asString(), operation);
                EXPECT_EQ(record["level"].asString(), level);
                EXPECT_EQ(record["message"].asString(), message);
                EXPECT_EQ(
                    record["message"].asString().find("instance_id="),
                    std::string::npos
                );
                for (const auto* field : {
                         "request_id",
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

        TEST(StorageWorkerRuntimeTest, ValidatesRequiredConfiguration) {
            const auto callback = []() -> drogon::Task<Result<StorageJobRunResult>> {
                co_return StorageJobRunResult{};
            };

            EXPECT_THROW(
                StorageWorkerRuntime(StorageWorkerRuntime::RunCallback{}),
                std::invalid_argument
            );
            EXPECT_THROW(
                StorageWorkerRuntime(
                    callback,
                    StorageWorkerRuntimeOptions{ .poll_interval_ms = 99 }
                ),
                std::invalid_argument
            );
        }

        TEST(StorageWorkerRuntimeTest, DrainsReadyWorkBeforeWaitingForNextTimer) {
            size_t calls = 0;
            StorageWorkerRuntime runtime(
                [&calls]() -> drogon::Task<Result<StorageJobRunResult>> {
                    calls++;
                    if (calls == 2) {
                        co_return StorageJobRunResult{};
                    }
                    co_return StorageJobRunResult{
                        .claimed = 2,
                        .succeeded = 1,
                        .retried = 1,
                    };
                }
            );

            EXPECT_TRUE(drogon::sync_wait(runtime.PollOnce()));
            EXPECT_EQ(calls, 2);
            EXPECT_TRUE(runtime.IsDrained());
        }

        TEST(StorageWorkerRuntimeTest, StopsContinuousDrainAfterRunnerFailure) {
            size_t calls = 0;
            StorageWorkerRuntime runtime(
                [&calls]() -> drogon::Task<Result<StorageJobRunResult>> {
                    calls++;
                    if (calls == 1) {
                        co_return StorageJobRunResult{ .claimed = 1, .succeeded = 1 };
                    }
                    co_return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "injected result failure")
                    );
                }
            );

            EXPECT_TRUE(drogon::sync_wait(runtime.PollOnce()));
            EXPECT_EQ(calls, 2);
            EXPECT_TRUE(runtime.IsDrained());
        }

        TEST(StorageWorkerRuntimeTest, ContainsRunnerFailuresAndCanPollAgain) {
            size_t calls = 0;
            StorageWorkerRuntime runtime(
                [&calls]() -> drogon::Task<Result<StorageJobRunResult>> {
                    calls++;
                    if (calls == 1) {
                        throw std::runtime_error("injected failure");
                    }
                    co_return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "injected result failure")
                    );
                }
            );

            EXPECT_TRUE(drogon::sync_wait(runtime.PollOnce()));
            EXPECT_TRUE(drogon::sync_wait(runtime.PollOnce()));
            EXPECT_EQ(calls, 2);
            EXPECT_TRUE(runtime.IsDrained());
        }

        TEST(StorageWorkerRuntimeTest, DrainPermanentlyStopsNewClaims) {
            size_t calls = 0;
            StorageWorkerRuntime runtime(
                [&calls]() -> drogon::Task<Result<StorageJobRunResult>> {
                    calls++;
                    co_return StorageJobRunResult{};
                }
            );

            runtime.BeginDrain();
            runtime.BeginDrain();

            EXPECT_TRUE(runtime.IsDrained());
            EXPECT_FALSE(drogon::sync_wait(runtime.PollOnce()));
            EXPECT_EQ(calls, 0);
        }

        TEST_F(StorageWorkerRuntimeLogTest, EmitsTypedContextWithoutFailureDetails) {
            size_t success_calls = 0;
            StorageWorkerRuntime successful_runtime(
                [&success_calls]() -> drogon::Task<Result<StorageJobRunResult>> {
                    success_calls++;
                    if (success_calls == 1) {
                        co_return StorageJobRunResult{
                            .claimed = 2,
                            .succeeded = 1,
                            .retried = 1,
                        };
                    }
                    co_return StorageJobRunResult{};
                }
            );
            EXPECT_TRUE(drogon::sync_wait(successful_runtime.PollOnce()));
            EXPECT_EQ(success_calls, 2U);

            StorageWorkerRuntime failed_runtime(
                []() -> drogon::Task<Result<StorageJobRunResult>> {
                    co_return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, std::string(RESULT_SECRET))
                    );
                }
            );
            EXPECT_TRUE(drogon::sync_wait(failed_runtime.PollOnce()));

            StorageWorkerRuntime throwing_runtime(
                []() -> drogon::Task<Result<StorageJobRunResult>> {
                    throw std::runtime_error(std::string(EXCEPTION_SECRET));
                    co_return StorageJobRunResult{};
                }
            );
            EXPECT_TRUE(drogon::sync_wait(throwing_runtime.PollOnce()));

            StorageWorkerRuntime draining_runtime(
                []() -> drogon::Task<Result<StorageJobRunResult>> {
                    co_return StorageJobRunResult{};
                }
            );
            draining_runtime.BeginDrain();
            draining_runtime.BeginDrain();

            const auto records = Records();
            ASSERT_EQ(records.size(), 4U);
            ExpectContext(
                records[0],
                "storage_worker_poll",
                "info",
                "Storage worker poll completed: claimed=2, succeeded=1, retried=1, dead_lettered=0, ownership_lost=0"
            );
            ExpectContext(
                records[1],
                "storage_worker_poll",
                "error",
                "Storage worker poll failed"
            );
            ExpectContext(
                records[2],
                "storage_worker_poll",
                "error",
                "Storage worker poll threw"
            );
            ExpectContext(
                records[3],
                "storage_worker_runtime",
                "info",
                "Storage worker runtime draining: in_flight=0"
            );

            const auto output = m_output.str();
            EXPECT_EQ(output.find(RESULT_SECRET), std::string::npos);
            EXPECT_EQ(output.find(EXCEPTION_SECRET), std::string::npos);
        }
    } // namespace
} // namespace disk::jobs
