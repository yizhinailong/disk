/**
 * @file TransactionRunner_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief TransactionRunner 错误映射测试
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "services/TransactionRunner.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <drogon/orm/Exception.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>

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

    } // namespace
} // namespace disk::file
