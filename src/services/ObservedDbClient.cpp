/**
 * @file ObservedDbClient.cpp
 * @brief Transparent Drogon database client metrics proxy
 */

#include "services/ObservedDbClient.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <drogon/orm/Exception.h>
#include <drogon/orm/SqlBinder.h>

namespace disk::metrics {
    namespace {

        [[nodiscard]] auto ClassifySqlState(std::string_view state) noexcept
            -> DependencyOutcome {
            if (state.starts_with("08") || state == "57P01" || state == "57P02" ||
                state == "57P03") {
                return DependencyOutcome::Connection;
            }
            if (state.starts_with("23")) {
                return DependencyOutcome::Conflict;
            }
            if (state.starts_with("40")) {
                return DependencyOutcome::Retryable;
            }
            return DependencyOutcome::Permanent;
        }

        auto ForwardSql(
            const drogon::orm::DbClientPtr& delegate,
            bool uses_pool,
            const char* sql,
            size_t sql_length,
            size_t parameter_count,
            std::vector<const char*>&& parameters,
            std::vector<int>&& lengths,
            std::vector<int>&& formats,
            drogon::orm::ResultCallback&& result_callback,
            std::function<void(const std::exception_ptr&)>&& exception_callback
        ) -> void {
            auto timer = std::make_shared<DependencyCallTimer>(Dependency::PostgreSql, uses_pool);
            try {
                auto binder = *delegate << std::string(sql, sql_length);
                for (size_t index = 0; index < parameter_count; ++index) {
                    binder << drogon::orm::RawParameter{
                        .obj = {},
                        .parameter = parameters[index],
                        .length = lengths[index],
                        .format = formats[index],
                    };
                }
                binder >> [timer, callback = std::move(result_callback)](
                              const drogon::orm::Result& result
                          ) mutable {
                    timer->Finish(DependencyOutcome::Success);
                    if (callback) {
                        callback(result);
                    }
                };
                binder >> [timer, callback = std::move(exception_callback)](
                              const std::exception_ptr& exception
                          ) mutable {
                    timer->Finish(ClassifyPostgreSqlException(exception));
                    if (callback) {
                        callback(exception);
                    }
                };
                binder.exec();
            } catch (...) {
                timer->Finish(ClassifyPostgreSqlException(std::current_exception()));
                throw;
            }
        }

        class ObservedTransaction final : public drogon::orm::Transaction {
        public:
            explicit ObservedTransaction(std::shared_ptr<drogon::orm::Transaction> delegate)
                : m_delegate(std::move(delegate)) {
                if (m_delegate == nullptr) {
                    throw std::invalid_argument("Observed transaction delegate is required");
                }
                type_ = m_delegate->type();
                connectionInfo_ = m_delegate->connectionInfo();
                MetricsRegistry::GetInstance().AcquireDependencyPoolLease(Dependency::PostgreSql);
            }

            ~ObservedTransaction() override {
                MetricsRegistry::GetInstance().ReleaseDependencyPoolLease(Dependency::PostgreSql);
            }

            auto rollback() -> void override {
                m_delegate->rollback();
            }

            auto setCommitCallback(const std::function<void(bool)>& commit_callback) -> void override {
                m_delegate->setCommitCallback(commit_callback);
            }

            [[nodiscard]] auto newTransaction(
                const std::function<void(bool)>& commit_callback = std::function<void(bool)>()
            ) -> std::shared_ptr<drogon::orm::Transaction> override {
                DependencyCallTimer timer(Dependency::PostgreSql);
                try {
                    auto transaction = m_delegate->newTransaction(commit_callback);
                    timer.Finish(DependencyOutcome::Success);
                    return std::make_shared<ObservedTransaction>(std::move(transaction));
                } catch (...) {
                    timer.Finish(ClassifyPostgreSqlException(std::current_exception()));
                    throw;
                }
            }

            auto newTransactionAsync(
                const std::function<void(const std::shared_ptr<drogon::orm::Transaction>&)>& callback
            ) -> void override {
                auto timer = std::make_shared<DependencyCallTimer>(Dependency::PostgreSql);
                try {
                    m_delegate->newTransactionAsync([timer, callback](const auto& transaction) {
                        if (transaction == nullptr) {
                            timer->Finish(DependencyOutcome::Timeout);
                            callback(nullptr);
                            return;
                        }
                        timer->Finish(DependencyOutcome::Success);
                        callback(std::make_shared<ObservedTransaction>(transaction));
                    });
                } catch (...) {
                    timer->Finish(ClassifyPostgreSqlException(std::current_exception()));
                    throw;
                }
            }

            [[nodiscard]] auto hasAvailableConnections() const noexcept -> bool override {
                return m_delegate->hasAvailableConnections();
            }

            auto setTimeout(double timeout) -> void override {
                m_delegate->setTimeout(timeout);
            }

            auto closeAll() -> void override {
                m_delegate->closeAll();
            }

        private:
            auto execSql(
                const char* sql,
                size_t sql_length,
                size_t parameter_count,
                std::vector<const char*>&& parameters,
                std::vector<int>&& lengths,
                std::vector<int>&& formats,
                drogon::orm::ResultCallback&& result_callback,
                std::function<void(const std::exception_ptr&)>&& exception_callback
            ) -> void override {
                ForwardSql(
                    m_delegate,
                    false,
                    sql,
                    sql_length,
                    parameter_count,
                    std::move(parameters),
                    std::move(lengths),
                    std::move(formats),
                    std::move(result_callback),
                    std::move(exception_callback)
                );
            }

            std::shared_ptr<drogon::orm::Transaction> m_delegate;
        };

    } // namespace

    auto ClassifyPostgreSqlException(const std::exception_ptr& exception) noexcept
        -> DependencyOutcome {
        if (exception == nullptr) {
            return DependencyOutcome::Other;
        }
        try {
            std::rethrow_exception(exception);
        } catch (const drogon::orm::TimeoutError&) {
            return DependencyOutcome::Timeout;
        } catch (const drogon::orm::BrokenConnection&) {
            return DependencyOutcome::Connection;
        } catch (const drogon::orm::InDoubtError&) {
            return DependencyOutcome::Connection;
        } catch (const drogon::orm::StatementCompletionUnknown&) {
            return DependencyOutcome::Connection;
        } catch (const drogon::orm::SerializationFailure&) {
            return DependencyOutcome::Retryable;
        } catch (const drogon::orm::DeadlockDetected&) {
            return DependencyOutcome::Retryable;
        } catch (const drogon::orm::TransactionRollback&) {
            return DependencyOutcome::Retryable;
        } catch (const drogon::orm::SqlError& error) {
            return ClassifySqlState(error.sqlState());
        } catch (const drogon::orm::UsageError&) {
            return DependencyOutcome::Protocol;
        } catch (const drogon::orm::ConversionError&) {
            return DependencyOutcome::Protocol;
        } catch (const drogon::orm::Failure&) {
            return DependencyOutcome::Other;
        } catch (...) {
            return DependencyOutcome::Other;
        }
    }

    ObservedDbClient::ObservedDbClient(drogon::orm::DbClientPtr delegate)
        : m_delegate(std::move(delegate)) {
        if (m_delegate == nullptr) {
            throw std::invalid_argument("Observed database client delegate is required");
        }
        type_ = m_delegate->type();
        connectionInfo_ = m_delegate->connectionInfo();
    }

    auto ObservedDbClient::newTransaction(const std::function<void(bool)>& commit_callback)
        -> std::shared_ptr<drogon::orm::Transaction> {
        DependencyCallTimer timer(Dependency::PostgreSql);
        try {
            auto transaction = m_delegate->newTransaction(commit_callback);
            timer.Finish(DependencyOutcome::Success);
            return std::make_shared<ObservedTransaction>(std::move(transaction));
        } catch (...) {
            timer.Finish(ClassifyPostgreSqlException(std::current_exception()));
            throw;
        }
    }

    auto ObservedDbClient::newTransactionAsync(
        const std::function<void(const std::shared_ptr<drogon::orm::Transaction>&)>& callback
    ) -> void {
        auto timer = std::make_shared<DependencyCallTimer>(Dependency::PostgreSql);
        try {
            m_delegate->newTransactionAsync([timer, callback](const auto& transaction) {
                if (transaction == nullptr) {
                    timer->Finish(DependencyOutcome::Timeout);
                    callback(nullptr);
                    return;
                }
                timer->Finish(DependencyOutcome::Success);
                callback(std::make_shared<ObservedTransaction>(transaction));
            });
        } catch (...) {
            timer->Finish(ClassifyPostgreSqlException(std::current_exception()));
            throw;
        }
    }

    auto ObservedDbClient::hasAvailableConnections() const noexcept -> bool {
        return m_delegate->hasAvailableConnections();
    }

    auto ObservedDbClient::setTimeout(double timeout) -> void {
        m_delegate->setTimeout(timeout);
    }

    auto ObservedDbClient::closeAll() -> void {
        m_delegate->closeAll();
    }

    auto ObservedDbClient::execSql(
        const char* sql,
        size_t sql_length,
        size_t parameter_count,
        std::vector<const char*>&& parameters,
        std::vector<int>&& lengths,
        std::vector<int>&& formats,
        drogon::orm::ResultCallback&& result_callback,
        std::function<void(const std::exception_ptr&)>&& exception_callback
    ) -> void {
        ForwardSql(
            m_delegate,
            true,
            sql,
            sql_length,
            parameter_count,
            std::move(parameters),
            std::move(lengths),
            std::move(formats),
            std::move(result_callback),
            std::move(exception_callback)
        );
    }

    auto ObserveDbClient(drogon::orm::DbClientPtr client) -> drogon::orm::DbClientPtr {
        if (client == nullptr || dynamic_cast<ObservedDbClient*>(client.get()) != nullptr) {
            return client;
        }
        return std::make_shared<ObservedDbClient>(std::move(client));
    }

} // namespace disk::metrics
