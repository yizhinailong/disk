/**
 * @file TransactionRunner_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief TransactionRunner 错误映射测试
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "services/TransactionRunner.hpp"

#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <drogon/orm/Exception.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>

namespace disk::file {
    namespace {

        class FakeTransaction final : public drogon::orm::Transaction {
        public:
            auto rollback() -> void override {
                ++rollback_count;
                if (rollback_should_throw) {
                    throw std::runtime_error("rollback implementation detail");
                }
            }

            auto setCommitCallback(const std::function<void(bool)>& commitCallback) -> void override {
                commit_callback = commitCallback;
            }

            auto newTransaction(
                const std::function<void(bool)>& commitCallback = std::function<void(bool)>()
            ) noexcept(false) -> std::shared_ptr<drogon::orm::Transaction> override {
                auto nested = std::make_shared<FakeTransaction>();
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

            int rollback_count = 0;
            bool rollback_should_throw = false;
            double last_timeout = 0.0;
            std::function<void(bool)> commit_callback;

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
        };

        class FakeDbClient final : public drogon::orm::DbClient {
        public:
            explicit FakeDbClient(std::shared_ptr<FakeTransaction> transaction)
                : transaction_(std::move(transaction)) {
            }

            auto newTransaction(
                const std::function<void(bool)>& commitCallback = std::function<void(bool)>()
            ) noexcept(false) -> std::shared_ptr<drogon::orm::Transaction> override {
                transaction_->setCommitCallback(commitCallback);
                return transaction_;
            }

            auto newTransactionAsync(
                const std::function<void(const std::shared_ptr<drogon::orm::Transaction>&)>& callback
            ) -> void override {
                callback(transaction_);
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

            std::shared_ptr<FakeTransaction> transaction_;
        };

        auto MakeRunner(std::shared_ptr<FakeTransaction> transaction) -> TransactionRunner {
            return TransactionRunner(std::make_shared<FakeDbClient>(std::move(transaction)));
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
            auto transaction = std::make_shared<FakeTransaction>();
            auto runner = MakeRunner(transaction);

            auto result = drogon::sync_wait(runner.Run(
                [](const drogon::orm::DbClientPtr& /*tx*/) -> drogon::Task<Result<void>> {
                    co_return co_await SuccessTask();
                }
            ));

            EXPECT_TRUE(result.has_value());
            EXPECT_EQ(transaction->rollback_count, 0);
        }

        TEST(TransactionRunner, CallbackErrorIsPreservedAndRollsBack) {
            auto transaction = std::make_shared<FakeTransaction>();
            auto runner = MakeRunner(transaction);

            auto result = drogon::sync_wait(runner.Run(
                [](const drogon::orm::DbClientPtr& /*tx*/) -> drogon::Task<Result<void>> {
                    co_return co_await CallbackErrorTask(ErrorInfo(ErrorCode::FolderNotFound));
                }
            ));

            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::FolderNotFound);
            EXPECT_EQ(result.error().message, Error::GetErrorMessage(ErrorCode::FolderNotFound));
            EXPECT_EQ(transaction->rollback_count, 1);
        }

        TEST(TransactionRunner, StdExceptionIsNormalizedAndRollsBack) {
            auto transaction = std::make_shared<FakeTransaction>();
            auto runner = MakeRunner(transaction);

            auto result = drogon::sync_wait(runner.Run(
                [](const drogon::orm::DbClientPtr& /*tx*/) -> drogon::Task<Result<void>> {
                    co_return co_await RuntimeErrorTask("raw implementation detail");
                }
            ));

            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::InternalError);
            EXPECT_EQ(result.error().message, Error::GetErrorMessage(ErrorCode::InternalError));
            EXPECT_EQ(result.error().message.find("raw implementation detail"), std::string::npos);
            EXPECT_EQ(transaction->rollback_count, 1);
        }

        TEST(TransactionRunner, DbExceptionIsNormalizedAndRollsBack) {
            auto transaction = std::make_shared<FakeTransaction>();
            auto runner = MakeRunner(transaction);

            auto result = drogon::sync_wait(runner.Run(
                [](const drogon::orm::DbClientPtr& /*tx*/) -> drogon::Task<Result<void>> {
                    co_return co_await DbErrorTask("raw database detail");
                }
            ));

            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::InternalError);
            EXPECT_EQ(result.error().message, Error::GetErrorMessage(ErrorCode::InternalError));
            EXPECT_EQ(result.error().message.find("raw database detail"), std::string::npos);
            EXPECT_EQ(transaction->rollback_count, 1);
        }

        TEST(TransactionRunner, RollbackFailureDoesNotReplaceCallbackError) {
            auto transaction = std::make_shared<FakeTransaction>();
            transaction->rollback_should_throw = true;
            auto runner = MakeRunner(transaction);

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
            EXPECT_EQ(transaction->rollback_count, 1);
        }

    } ///< namespace
} ///< namespace disk::file
