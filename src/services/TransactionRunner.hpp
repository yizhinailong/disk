/**
 * @file TransactionRunner.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 数据库事务运行器
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <atomic>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <memory>
#include <utility>

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>
#include <trantor/net/EventLoop.h>

#include "utils/ErrorCode.hpp"
#include "utils/LogHelper.hpp"

namespace disk::file {

    namespace transaction_internal {

        class BeginAwaiter final {
        public:
            explicit BeginAwaiter(drogon::orm::DbClientPtr db_client)
                : m_db_client(std::move(db_client)) {
            }

            [[nodiscard]] auto await_ready() const noexcept -> bool {
                return false;
            }

            auto await_suspend(std::coroutine_handle<> handle) -> bool {
                m_db_client->newTransactionAsync(
                    [this, handle](const std::shared_ptr<drogon::orm::Transaction>& transaction) {
                        m_transaction = transaction;
                        uint8_t expected = INITIALIZING;
                        if (m_state.compare_exchange_strong(
                                expected,
                                COMPLETED,
                                std::memory_order_acq_rel,
                                std::memory_order_acquire
                            )) {
                            return;
                        }
                        if (expected == SUSPENDED) {
                            handle.resume();
                        }
                    }
                );

                uint8_t expected = INITIALIZING;
                if (m_state.compare_exchange_strong(
                        expected,
                        SUSPENDED,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire
                    )) {
                    return true;
                }
                return false;
            }

            [[nodiscard]] auto await_resume() -> std::shared_ptr<drogon::orm::Transaction> {
                if (!m_transaction) {
                    throw drogon::orm::TimeoutError(
                        "Timeout, no connection available for transaction"
                    );
                }
                return std::move(m_transaction);
            }

        private:
            static constexpr uint8_t INITIALIZING = 0;
            static constexpr uint8_t SUSPENDED = 1;
            static constexpr uint8_t COMPLETED = 2;

            drogon::orm::DbClientPtr m_db_client;
            std::shared_ptr<drogon::orm::Transaction> m_transaction;
            std::atomic<uint8_t> m_state{ INITIALIZING };
        };

        class CommitAwaiter final {
        public:
            explicit CommitAwaiter(std::shared_ptr<drogon::orm::Transaction>& transaction)
                : m_transaction(std::move(transaction)) {
            }

            [[nodiscard]] auto await_ready() const noexcept -> bool {
                return false;
            }

            auto await_suspend(std::coroutine_handle<> handle) -> bool {
                m_transaction->setCommitCallback([this, handle](bool succeeded) {
                    m_succeeded = succeeded;
                    uint8_t expected = INITIALIZING;
                    if (m_state.compare_exchange_strong(
                            expected,
                            COMPLETED,
                            std::memory_order_acq_rel,
                            std::memory_order_acquire
                        )) {
                        return;
                    }
                    if (expected == SUSPENDED) {
                        handle.resume();
                    }
                });

                m_transaction.reset();

                uint8_t expected = INITIALIZING;
                if (m_state.compare_exchange_strong(
                        expected,
                        SUSPENDED,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire
                    )) {
                    return true;
                }
                return false;
            }

            [[nodiscard]] auto await_resume() const noexcept -> bool {
                return m_succeeded;
            }

        private:
            static constexpr uint8_t INITIALIZING = 0;
            static constexpr uint8_t SUSPENDED = 1;
            static constexpr uint8_t COMPLETED = 2;

            std::shared_ptr<drogon::orm::Transaction> m_transaction;
            std::atomic<uint8_t> m_state{ INITIALIZING };
            bool m_succeeded{ false };
        };

    } // namespace transaction_internal

    /**
     * @brief 数据库事务运行器
     *
     * 仅负责数据库事务 begin/rollback 和错误映射；文件系统补偿保持在调用方显式处理。
     * 映射规则：callback 返回的 ErrorInfo 是公开领域错误，rollback 后原样返回；
     * DB/普通异常仅记录内部细节，对客户端统一返回默认 InternalError；rollback 失败只记录日志。
     */
    class TransactionRunner {
    public:
        explicit TransactionRunner(
            drogon::orm::DbClientPtr db_client,
            ErrorInfo default_error = ErrorInfo(ErrorCode::InternalError),
            disk::utils::LogContext log_context = {}
        )
            : m_db_client(std::move(db_client)),
              m_default_error(std::move(default_error)),
              m_log_context(std::move(log_context)) {
        }

        explicit TransactionRunner(
            drogon::orm::DbClientPtr db_client,
            disk::utils::LogContext log_context
        )
            : TransactionRunner(
                  std::move(db_client),
                  ErrorInfo(ErrorCode::InternalError),
                  std::move(log_context)
              ) {
        }

        template <typename Func>
        [[nodiscard]]
        auto Run(Func&& func) const -> drogon::Task<Result<void>> {
            std::shared_ptr<drogon::orm::Transaction> transaction;
            try {
                transaction = co_await Begin(m_db_client);
                auto transaction_client = std::static_pointer_cast<drogon::orm::DbClient>(transaction);
                auto result = co_await std::forward<Func>(func)(transaction_client);
                transaction_client.reset();
                if (!result) {
                    rollbackQuietly(transaction, m_log_context);
                    co_return std::unexpected(result.error());
                }

                auto commit_result = co_await Commit(transaction, m_log_context);
                if (!commit_result) {
                    rollbackQuietly(transaction, m_log_context);
                    Logger::Error(m_log_context) << "Database transaction commit failed";
                    co_return std::unexpected(m_default_error);
                }
                co_return {};
            } catch (const drogon::orm::DrogonDbException& e) {
                rollbackQuietly(transaction, m_log_context);
                Logger::Error(m_log_context) << "Database transaction failed: " << e.base().what();
                co_return std::unexpected(m_default_error);
            } catch (const std::exception& e) {
                rollbackQuietly(transaction, m_log_context);
                Logger::Error(m_log_context) << "Database transaction failed: " << e.what();
                co_return std::unexpected(m_default_error);
            }
        }

        [[nodiscard]]
        static auto Begin(const drogon::orm::DbClientPtr& db_client)
            -> transaction_internal::BeginAwaiter {
            return transaction_internal::BeginAwaiter(db_client);
        }

        [[nodiscard]]
        static auto Commit(
            std::shared_ptr<drogon::orm::Transaction>& transaction,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<void>> {
            if (!transaction) {
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::InternalError,
                    "Database transaction is not available"
                ));
            }

            auto* current_loop = trantor::EventLoop::getEventLoopOfCurrentThread();
            constexpr uint8_t kMaxOwnerReleaseYields = 3;
            for (uint8_t attempt = 0;
                 transaction.use_count() != 1 && current_loop != nullptr &&
                 attempt < kMaxOwnerReleaseYields;
                 ++attempt) {
                co_await drogon::sleepCoro(current_loop, 0.0);
            }

            if (transaction.use_count() != 1) {
                Logger::Error(log_context)
                    << "Database transaction has outstanding owners at commit: count="
                    << transaction.use_count();
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::InternalError,
                    "Database transaction cannot be committed"
                ));
            }

            const auto committed = co_await transaction_internal::CommitAwaiter(transaction);
            if (!committed) {
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::InternalError,
                    "Database transaction commit failed"
                ));
            }

            co_return {};
        }

    private:
        static auto rollbackQuietly(
            const std::shared_ptr<drogon::orm::Transaction>& transaction,
            const disk::utils::LogContext& log_context
        ) -> void {
            if (!transaction) {
                return;
            }

            try {
                transaction->rollback();
            } catch (const std::exception& e) {
                Logger::Error(log_context) << "Transaction rollback failed: " << e.what();
            }
        }

        drogon::orm::DbClientPtr m_db_client;
        ErrorInfo m_default_error;
        disk::utils::LogContext m_log_context;
    };

} // namespace disk::file
