#include "services/ObservedDbClient.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <drogon/orm/Exception.h>
#include <gtest/gtest.h>

namespace disk::metrics {
    namespace {
        struct FakeDbState final {
            std::string sql;
            size_t parameter_count{ 0 };
            std::vector<int> lengths;
            std::vector<int> formats;
            std::vector<std::vector<uint8_t>> parameters;
            std::exception_ptr failure;
            double timeout{ 0.0 };
            int close_all_calls{ 0 };
        };

        auto ExecuteFakeSql(
            const std::shared_ptr<FakeDbState>& state,
            const char* sql,
            size_t sql_length,
            size_t parameter_count,
            std::vector<const char*>&& parameters,
            std::vector<int>&& lengths,
            std::vector<int>&& formats,
            drogon::orm::ResultCallback&& result_callback,
            std::function<void(const std::exception_ptr&)>&& exception_callback
        ) -> void {
            state->sql.assign(sql, sql_length);
            state->parameter_count = parameter_count;
            state->lengths = lengths;
            state->formats = formats;
            state->parameters.clear();
            state->parameters.reserve(parameter_count);
            for (size_t index = 0; index < parameter_count; ++index) {
                const auto* begin = reinterpret_cast<const uint8_t*>(parameters[index]);
                state->parameters.emplace_back(begin, begin + lengths[index]);
            }

            if (state->failure != nullptr) {
                exception_callback(state->failure);
                return;
            }
            result_callback(drogon::orm::Result(nullptr));
        }

        class FakeTransaction final : public drogon::orm::Transaction {
        public:
            explicit FakeTransaction(std::shared_ptr<FakeDbState> state)
                : m_state(std::move(state)) {
                type_ = drogon::orm::ClientType::PostgreSQL;
                connectionInfo_ = "fake-transaction";
            }

            auto rollback() -> void override {
            }

            auto setCommitCallback(const std::function<void(bool)>& commit_callback) -> void override {
                m_commit_callback = commit_callback;
            }

            [[nodiscard]] auto newTransaction(
                const std::function<void(bool)>& commit_callback = std::function<void(bool)>()
            ) -> std::shared_ptr<drogon::orm::Transaction> override {
                auto transaction = std::make_shared<FakeTransaction>(m_state);
                transaction->setCommitCallback(commit_callback);
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
                m_state->timeout = timeout;
            }

            auto closeAll() -> void override {
                ++m_state->close_all_calls;
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
                ExecuteFakeSql(
                    m_state,
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

            std::shared_ptr<FakeDbState> m_state;
            std::function<void(bool)> m_commit_callback;
        };

        class FakeDbClient final : public drogon::orm::DbClient {
        public:
            explicit FakeDbClient(std::shared_ptr<FakeDbState> state)
                : m_state(std::move(state)) {
                type_ = drogon::orm::ClientType::PostgreSQL;
                connectionInfo_ = "fake-client";
            }

            [[nodiscard]] auto newTransaction(
                const std::function<void(bool)>& commit_callback = std::function<void(bool)>()
            ) -> std::shared_ptr<drogon::orm::Transaction> override {
                auto transaction = std::make_shared<FakeTransaction>(m_state);
                transaction->setCommitCallback(commit_callback);
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
                m_state->timeout = timeout;
            }

            auto closeAll() -> void override {
                ++m_state->close_all_calls;
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
                ExecuteFakeSql(
                    m_state,
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

            std::shared_ptr<FakeDbState> m_state;
        };

        TEST(ObservedDbClientTest, ClassifiesPostgreSqlErrorsIntoFixedOutcomes) {
            EXPECT_EQ(
                ClassifyPostgreSqlException(
                    std::make_exception_ptr(drogon::orm::TimeoutError("timeout"))
                ),
                DependencyOutcome::Timeout
            );
            EXPECT_EQ(
                ClassifyPostgreSqlException(
                    std::make_exception_ptr(drogon::orm::BrokenConnection("connection"))
                ),
                DependencyOutcome::Connection
            );
            EXPECT_EQ(
                ClassifyPostgreSqlException(std::make_exception_ptr(drogon::orm::SqlError("unique", "INSERT", "23505"))),
                DependencyOutcome::Conflict
            );
            EXPECT_EQ(
                ClassifyPostgreSqlException(std::make_exception_ptr(drogon::orm::SqlError("serialization", "UPDATE", "40001"))),
                DependencyOutcome::Retryable
            );
            EXPECT_EQ(
                ClassifyPostgreSqlException(
                    std::make_exception_ptr(drogon::orm::UsageError("bad binder"))
                ),
                DependencyOutcome::Protocol
            );
        }

        TEST(ObservedDbClientTest, PreservesSqlParametersAndRecordsSuccess) {
            auto state = std::make_shared<FakeDbState>();
            auto fake = std::make_shared<FakeDbClient>(state);
            auto observed = ObserveDbClient(fake);
            const auto dependency = static_cast<size_t>(Dependency::PostgreSql);
            const auto success = static_cast<size_t>(DependencyOutcome::Success);
            const auto before = MetricsRegistry::GetInstance().Snapshot();

            static_cast<void>(
                observed->execSqlAsyncFuture("SELECT $1::integer, $2::text", 42, std::string("abc"))
                    .get()
            );
            const auto after = MetricsRegistry::GetInstance().Snapshot();

            EXPECT_EQ(state->sql, "SELECT $1::integer, $2::text");
            ASSERT_EQ(state->parameter_count, 2);
            EXPECT_EQ(state->lengths, (std::vector<int>{ 4, 3 }));
            EXPECT_EQ(state->formats, (std::vector<int>{ 1, 0 }));
            EXPECT_EQ(state->parameters[0], (std::vector<uint8_t>{ 0, 0, 0, 42 }));
            EXPECT_EQ(state->parameters[1], (std::vector<uint8_t>{ 'a', 'b', 'c' }));
            EXPECT_EQ(
                after.dependency_calls[dependency][success],
                before.dependency_calls[dependency][success] + 1
            );
            EXPECT_EQ(
                after.dependency_calls_inflight[dependency],
                before.dependency_calls_inflight[dependency]
            );
            EXPECT_EQ(
                after.dependency_pool_demand[dependency],
                before.dependency_pool_demand[dependency]
            );

            observed->setTimeout(2.5);
            observed->closeAll();
            EXPECT_DOUBLE_EQ(state->timeout, 2.5);
            EXPECT_EQ(state->close_all_calls, 1);
            EXPECT_EQ(ObserveDbClient(observed), observed);
        }

        TEST(ObservedDbClientTest, RecordsTimeoutAndRestoresPoolDemand) {
            auto state = std::make_shared<FakeDbState>();
            state->failure = std::make_exception_ptr(drogon::orm::TimeoutError("timeout"));
            auto observed = ObserveDbClient(std::make_shared<FakeDbClient>(state));
            const auto dependency = static_cast<size_t>(Dependency::PostgreSql);
            const auto timeout = static_cast<size_t>(DependencyOutcome::Timeout);
            const auto before = MetricsRegistry::GetInstance().Snapshot();

            EXPECT_THROW(
                static_cast<void>(observed->execSqlAsyncFuture("SELECT 1").get()),
                drogon::orm::TimeoutError
            );
            const auto after = MetricsRegistry::GetInstance().Snapshot();

            EXPECT_EQ(
                after.dependency_calls[dependency][timeout],
                before.dependency_calls[dependency][timeout] + 1
            );
            EXPECT_EQ(
                after.dependency_calls_inflight[dependency],
                before.dependency_calls_inflight[dependency]
            );
            EXPECT_EQ(
                after.dependency_pool_demand[dependency],
                before.dependency_pool_demand[dependency]
            );
        }

        TEST(ObservedDbClientTest, AllowsFireAndForgetBinderWithoutResultCallback) {
            auto state = std::make_shared<FakeDbState>();
            auto observed = ObserveDbClient(std::make_shared<FakeDbClient>(state));
            const auto dependency = static_cast<size_t>(Dependency::PostgreSql);
            const auto success = static_cast<size_t>(DependencyOutcome::Success);
            const auto before = MetricsRegistry::GetInstance().Snapshot();

            auto binder = *observed << "ANALYZE";
            EXPECT_NO_THROW(binder.exec());

            const auto after = MetricsRegistry::GetInstance().Snapshot();
            EXPECT_EQ(state->sql, "ANALYZE");
            EXPECT_EQ(
                after.dependency_calls[dependency][success],
                before.dependency_calls[dependency][success] + 1
            );
        }

        TEST(ObservedDbClientTest, TracksTransactionLeaseUntilProxyIsDestroyed) {
            auto state = std::make_shared<FakeDbState>();
            auto observed = ObserveDbClient(std::make_shared<FakeDbClient>(state));
            auto& registry = MetricsRegistry::GetInstance();
            const auto dependency = static_cast<size_t>(Dependency::PostgreSql);
            const auto success = static_cast<size_t>(DependencyOutcome::Success);
            const auto before = registry.Snapshot();

            {
                auto transaction = observed->newTransaction();
                const auto acquired = registry.Snapshot();
                EXPECT_EQ(
                    acquired.dependency_pool_leases[dependency],
                    before.dependency_pool_leases[dependency] + 1
                );
                static_cast<void>(transaction->execSqlAsyncFuture("SELECT 1").get());
                transaction->closeAll();
            }

            const auto after = registry.Snapshot();
            EXPECT_EQ(
                after.dependency_pool_leases[dependency],
                before.dependency_pool_leases[dependency]
            );
            EXPECT_EQ(
                after.dependency_calls[dependency][success],
                before.dependency_calls[dependency][success] + 2
            );
            EXPECT_EQ(state->close_all_calls, 1);
        }
    } // namespace
} // namespace disk::metrics
