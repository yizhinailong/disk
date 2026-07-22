/**
 * @file TransactionRunner_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief TransactionRunner 错误映射测试
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "services/TransactionRunner.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <drogon/orm/Exception.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>
#include <json/json.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

namespace disk::file {
    namespace {

        struct FakeTransactionState {
            int rollback_count{ 0 };
            int commit_count{ 0 };
            bool rollback_should_throw{ false };
            bool commit_succeeds{ true };
            bool defer_commit{ false };
            std::mutex commit_mutex;
            std::condition_variable commit_ready;
            std::function<void(bool)> pending_commit;

            auto PublishCommit(std::function<void(bool)> callback) -> void {
                {
                    std::lock_guard lock(commit_mutex);
                    pending_commit = std::move(callback);
                }
                commit_ready.notify_all();
            }

            [[nodiscard]] auto WaitForPendingCommit() -> bool {
                std::unique_lock lock(commit_mutex);
                return commit_ready.wait_for(lock, std::chrono::seconds(1), [this]() {
                    return static_cast<bool>(pending_commit);
                });
            }

            auto CompletePendingCommit() -> void {
                std::function<void(bool)> callback;
                {
                    std::lock_guard lock(commit_mutex);
                    callback = std::move(pending_commit);
                }
                if (callback) {
                    callback(commit_succeeds);
                }
            }
        };

        class FakeTransaction final : public drogon::orm::Transaction {
        public:
            explicit FakeTransaction(std::shared_ptr<FakeTransactionState> state)
                : m_state(std::move(state)) {
            }

            ~FakeTransaction() override {
                if (!m_rolled_back && commit_callback) {
                    ++m_state->commit_count;
                    if (m_state->defer_commit) {
                        m_state->PublishCommit(std::move(commit_callback));
                    } else {
                        commit_callback(m_state->commit_succeeds);
                    }
                }
            }

            auto rollback() -> void override {
                m_rolled_back = true;
                ++m_state->rollback_count;
                if (m_state->rollback_should_throw) {
                    throw std::runtime_error("rollback implementation detail");
                }
            }

            auto setCommitCallback(const std::function<void(bool)>& commitCallback) -> void override {
                commit_callback = commitCallback;
            }

            auto newTransaction(
                const std::function<void(bool)>& commitCallback = std::function<void(bool)>()
            ) noexcept(false) -> std::shared_ptr<drogon::orm::Transaction> override {
                auto nested = std::make_shared<FakeTransaction>(m_state);
                nested->setCommitCallback(commitCallback);
                return nested;
            }

            auto newTransactionAsync(
                const std::function<void(const std::shared_ptr<drogon::orm::Transaction>&)>& callback
            ) -> void override {
                callback(newTransaction());
            }

            [[nodiscard]] auto hasAvailableConnections() const noexcept -> bool override {
                return true;
            }

            auto setTimeout(double timeout) -> void override {
                last_timeout = timeout;
            }

            double last_timeout = 0.0;
            std::function<void(bool)> commit_callback;

        private:
            std::shared_ptr<FakeTransactionState> m_state;
            bool m_rolled_back{ false };

            auto execSql(
                const char* /*sql*/,
                size_t /*sqlLength*/,
                size_t /*paraNum*/,
                std::vector<const char*>&& /*parameters*/,
                std::vector<int>&& /*length*/,
                std::vector<int>&& /*format*/,
                drogon::orm::ResultCallback&& /*rcb*/,
                std::function<void(const std::exception_ptr&)>&& /*exceptCallback*/
            ) -> void override {
            }
        };

        class FakeDbClient final : public drogon::orm::DbClient {
        public:
            explicit FakeDbClient(std::shared_ptr<FakeTransactionState> state)
                : m_state(std::move(state)) {
            }

            auto newTransaction(
                const std::function<void(bool)>& commitCallback = std::function<void(bool)>()
            ) noexcept(false) -> std::shared_ptr<drogon::orm::Transaction> override {
                auto transaction = std::make_shared<FakeTransaction>(m_state);
                transaction->setCommitCallback(commitCallback);
                return transaction;
            }

            auto newTransactionAsync(
                const std::function<void(const std::shared_ptr<drogon::orm::Transaction>&)>& callback
            ) -> void override {
                callback(newTransaction());
            }

            [[nodiscard]] auto hasAvailableConnections() const noexcept -> bool override {
                return true;
            }

            auto setTimeout(double timeout) -> void override {
                last_timeout = timeout;
            }

            auto closeAll() -> void override {
                close_all_called = true;
            }

            double last_timeout = 0.0;
            bool close_all_called = false;

        private:
            auto execSql(
                const char* /*sql*/,
                size_t /*sqlLength*/,
                size_t /*paraNum*/,
                std::vector<const char*>&& /*parameters*/,
                std::vector<int>&& /*length*/,
                std::vector<int>&& /*format*/,
                drogon::orm::ResultCallback&& /*rcb*/,
                std::function<void(const std::exception_ptr&)>&& /*exceptCallback*/
            ) -> void override {
            }

            std::shared_ptr<FakeTransactionState> m_state;
        };

        auto MakeRunner(std::shared_ptr<FakeTransactionState> state) -> TransactionRunner {
            return TransactionRunner(std::make_shared<FakeDbClient>(std::move(state)));
        }

        class TransactionRunnerLogTest : public ::testing::Test {
        protected:
            auto SetUp() -> void override {
                m_previous_logger = spdlog::default_logger();
                m_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(m_output);
                m_logger = std::make_shared<spdlog::logger>("transaction-runner-test", m_sink);
                m_logger->set_level(spdlog::level::info);
                disk::utils::Logger::ApplyStructuredFormatter(m_logger);
                spdlog::set_default_logger(m_logger);
                disk::utils::Logger::SetInstanceId("transaction-test-instance");
            }

            auto TearDown() -> void override {
                disk::utils::Logger::SetInstanceId("");
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

            auto ResetOutput() -> void {
                m_logger->flush();
                m_output.str("");
                m_output.clear();
            }

            std::shared_ptr<spdlog::logger> m_previous_logger;
            std::ostringstream m_output;
            std::shared_ptr<spdlog::sinks::ostream_sink_mt> m_sink;
            std::shared_ptr<spdlog::logger> m_logger;
        };

        auto SuccessTask() -> drogon::Task<Result<void>> {
            co_return {};
        }

        auto CallbackErrorTask(ErrorInfo error) -> drogon::Task<Result<void>> {
            co_return std::unexpected(std::move(error));
        }

        auto RuntimeErrorTask(const std::string& message) -> drogon::Task<Result<void>> {
            throw std::runtime_error(message);
            co_return {};
        }

        auto DbErrorTask(const std::string& message) -> drogon::Task<Result<void>> {
            throw drogon::orm::SqlError(message, "select secret from files", "XX000");
            co_return {};
        }

        TEST(TransactionRunner, SuccessDoesNotRollback) {
            auto state = std::make_shared<FakeTransactionState>();
            auto runner = MakeRunner(state);

            auto result = drogon::sync_wait(runner.Run(
                [](const drogon::orm::DbClientPtr& /*tx*/) -> drogon::Task<Result<void>> {
                    co_return co_await SuccessTask();
                }
            ));

            EXPECT_TRUE(result.has_value());
            EXPECT_EQ(state->rollback_count, 0);
            EXPECT_EQ(state->commit_count, 1);
        }

        TEST(TransactionRunner, CallbackErrorIsPreservedAndRollsBack) {
            auto state = std::make_shared<FakeTransactionState>();
            auto runner = MakeRunner(state);

            auto result = drogon::sync_wait(runner.Run(
                [](const drogon::orm::DbClientPtr& /*tx*/) -> drogon::Task<Result<void>> {
                    co_return co_await CallbackErrorTask(ErrorInfo(ErrorCode::FolderNotFound));
                }
            ));

            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::FolderNotFound);
            EXPECT_EQ(result.error().message, Error::GetErrorMessage(ErrorCode::FolderNotFound));
            EXPECT_EQ(state->rollback_count, 1);
            EXPECT_EQ(state->commit_count, 0);
        }

        TEST(TransactionRunner, StdExceptionIsNormalizedAndRollsBack) {
            auto state = std::make_shared<FakeTransactionState>();
            auto runner = MakeRunner(state);

            auto result = drogon::sync_wait(runner.Run(
                [](const drogon::orm::DbClientPtr& /*tx*/) -> drogon::Task<Result<void>> {
                    co_return co_await RuntimeErrorTask("raw implementation detail");
                }
            ));

            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::InternalError);
            EXPECT_EQ(result.error().message, Error::GetErrorMessage(ErrorCode::InternalError));
            EXPECT_EQ(result.error().message.find("raw implementation detail"), std::string::npos);
            EXPECT_EQ(state->rollback_count, 1);
            EXPECT_EQ(state->commit_count, 0);
        }

        TEST(TransactionRunner, DbExceptionIsNormalizedAndRollsBack) {
            auto state = std::make_shared<FakeTransactionState>();
            auto runner = MakeRunner(state);

            auto result = drogon::sync_wait(runner.Run(
                [](const drogon::orm::DbClientPtr& /*tx*/) -> drogon::Task<Result<void>> {
                    co_return co_await DbErrorTask("raw database detail");
                }
            ));

            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::InternalError);
            EXPECT_EQ(result.error().message, Error::GetErrorMessage(ErrorCode::InternalError));
            EXPECT_EQ(result.error().message.find("raw database detail"), std::string::npos);
            EXPECT_EQ(state->rollback_count, 1);
            EXPECT_EQ(state->commit_count, 0);
        }

        TEST(TransactionRunner, CustomDefaultErrorMapsExceptions) {
            auto state = std::make_shared<FakeTransactionState>();
            auto runner = TransactionRunner(
                std::make_shared<FakeDbClient>(state),
                ErrorInfo(ErrorCode::InternalError, "Failed to move items")
            );

            auto result = drogon::sync_wait(runner.Run(
                [](const drogon::orm::DbClientPtr& /*tx*/) -> drogon::Task<Result<void>> {
                    co_return co_await DbErrorTask("raw database detail");
                }
            ));

            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::InternalError);
            EXPECT_EQ(result.error().message, "Failed to move items");
            EXPECT_EQ(result.error().message.find("raw database detail"), std::string::npos);
            EXPECT_EQ(state->rollback_count, 1);
            EXPECT_EQ(state->commit_count, 0);
        }

        TEST(TransactionRunner, RollbackFailureDoesNotReplaceCallbackError) {
            auto state = std::make_shared<FakeTransactionState>();
            state->rollback_should_throw = true;
            auto runner = MakeRunner(state);

            auto result = drogon::sync_wait(runner.Run(
                [](const drogon::orm::DbClientPtr& /*tx*/) -> drogon::Task<Result<void>> {
                    co_return co_await CallbackErrorTask(
                        ErrorInfo(ErrorCode::ValidationFailed, "domain validation failed")
                    );
                }
            ));

            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
            EXPECT_EQ(result.error().message, "domain validation failed");
            EXPECT_EQ(state->rollback_count, 1);
            EXPECT_EQ(state->commit_count, 0);
        }

        TEST(TransactionRunner, CommitFailureIsNormalized) {
            auto state = std::make_shared<FakeTransactionState>();
            state->commit_succeeds = false;
            auto runner = MakeRunner(state);

            auto result = drogon::sync_wait(runner.Run(
                [](const drogon::orm::DbClientPtr& /*tx*/) -> drogon::Task<Result<void>> {
                    co_return co_await SuccessTask();
                }
            ));

            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::InternalError);
            EXPECT_EQ(state->rollback_count, 0);
            EXPECT_EQ(state->commit_count, 1);
        }

        TEST(TransactionRunner, DoesNotReturnBeforeCommitCallback) {
            auto state = std::make_shared<FakeTransactionState>();
            state->defer_commit = true;
            auto runner = MakeRunner(state);

            auto future = std::async(std::launch::async, [runner = std::move(runner)]() {
                return drogon::sync_wait(runner.Run(
                    [](const drogon::orm::DbClientPtr& /*tx*/) -> drogon::Task<Result<void>> {
                        co_return co_await SuccessTask();
                    }
                ));
            });

            ASSERT_TRUE(state->WaitForPendingCommit());
            EXPECT_EQ(future.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);

            state->CompletePendingCommit();
            auto result = future.get();
            EXPECT_TRUE(result.has_value());
            EXPECT_EQ(state->commit_count, 1);
        }

        TEST(TransactionRunner, RejectsPersistentOutstandingOwner) {
            auto state = std::make_shared<FakeTransactionState>();
            std::shared_ptr<drogon::orm::Transaction> transaction =
                std::make_shared<FakeTransaction>(state);
            auto persistent_owner = transaction;

            auto result = drogon::sync_wait(TransactionRunner::Commit(transaction));

            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::InternalError);
            EXPECT_NE(transaction, nullptr);
            EXPECT_EQ(state->commit_count, 0);
            persistent_owner.reset();
            transaction.reset();
        }

        TEST_F(TransactionRunnerLogTest, FailureEventsPreserveExplicitContextAndNullDefaults) {
            const disk::utils::LogContext log_context{
                .request_id = "transaction-request",
                .operation = "upload_complete",
                .upload_id = "upload-123",
                .job_id = 42,
                .lease_owner = "api-a",
                .state_version = 7,
            };

            auto db_state = std::make_shared<FakeTransactionState>();
            db_state->rollback_should_throw = true;
            auto db_runner = TransactionRunner(
                std::make_shared<FakeDbClient>(db_state),
                log_context
            );
            EXPECT_FALSE(drogon::sync_wait(db_runner.Run([](const drogon::orm::DbClientPtr& /*tx*/) -> drogon::Task<Result<void>> {
                             co_return co_await DbErrorTask("raw database detail");
                         })).has_value());

            auto std_state = std::make_shared<FakeTransactionState>();
            auto std_runner = TransactionRunner(
                std::make_shared<FakeDbClient>(std_state),
                log_context
            );
            EXPECT_FALSE(drogon::sync_wait(std_runner.Run([](const drogon::orm::DbClientPtr& /*tx*/) -> drogon::Task<Result<void>> {
                             co_return co_await RuntimeErrorTask("raw implementation detail");
                         })).has_value());

            auto commit_state = std::make_shared<FakeTransactionState>();
            commit_state->commit_succeeds = false;
            auto commit_runner = TransactionRunner(
                std::make_shared<FakeDbClient>(commit_state),
                log_context
            );
            EXPECT_FALSE(drogon::sync_wait(commit_runner.Run([](const drogon::orm::DbClientPtr& /*tx*/) -> drogon::Task<Result<void>> {
                             co_return co_await SuccessTask();
                         })).has_value());

            auto owner_state = std::make_shared<FakeTransactionState>();
            std::shared_ptr<drogon::orm::Transaction> transaction =
                std::make_shared<FakeTransaction>(owner_state);
            auto persistent_owner = transaction;
            EXPECT_FALSE(drogon::sync_wait(TransactionRunner::Commit(transaction, log_context)).has_value());
            persistent_owner.reset();
            transaction.reset();

            const auto records = Records();
            ASSERT_EQ(records.size(), 5U);
            for (const auto& record : records) {
                EXPECT_EQ(record["request_id"].asString(), "transaction-request");
                EXPECT_EQ(record["instance_id"].asString(), "transaction-test-instance");
                EXPECT_EQ(record["operation"].asString(), "upload_complete");
                EXPECT_EQ(record["upload_id"].asString(), "upload-123");
                EXPECT_EQ(record["job_id"].asUInt64(), 42U);
                EXPECT_EQ(record["lease_owner"].asString(), "api-a");
                EXPECT_EQ(record["state_version"].asUInt64(), 7U);
            }

            const auto has_message = [&](std::string_view marker) {
                return std::ranges::any_of(records, [marker](const Json::Value& record) {
                    return record["message"].asString().find(marker) != std::string::npos;
                });
            };
            EXPECT_TRUE(has_message("Transaction rollback failed"));
            EXPECT_TRUE(has_message("Database transaction failed: raw database detail"));
            EXPECT_TRUE(has_message("Database transaction failed: raw implementation detail"));
            EXPECT_TRUE(has_message("Database transaction commit failed"));
            EXPECT_TRUE(has_message("Database transaction has outstanding owners at commit"));

            ResetOutput();
            auto default_state = std::make_shared<FakeTransactionState>();
            auto default_runner = MakeRunner(default_state);
            EXPECT_FALSE(drogon::sync_wait(default_runner.Run([](const drogon::orm::DbClientPtr& /*tx*/) -> drogon::Task<Result<void>> {
                             co_return co_await RuntimeErrorTask("default context detail");
                         })).has_value());

            const auto default_records = Records();
            ASSERT_EQ(default_records.size(), 1U);
            const auto& default_record = default_records.front();
            EXPECT_TRUE(default_record["request_id"].isNull());
            EXPECT_EQ(default_record["instance_id"].asString(), "transaction-test-instance");
            EXPECT_TRUE(default_record["operation"].isNull());
            EXPECT_TRUE(default_record["upload_id"].isNull());
            EXPECT_TRUE(default_record["job_id"].isNull());
            EXPECT_TRUE(default_record["lease_owner"].isNull());
            EXPECT_TRUE(default_record["state_version"].isNull());
        }

    } // namespace
} // namespace disk::file
