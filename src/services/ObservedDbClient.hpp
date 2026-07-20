/**
 * @file ObservedDbClient.hpp
 * @brief Transparent Drogon database client metrics proxy
 */

#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "services/MetricsService.hpp"

namespace disk::metrics {

    [[nodiscard]] auto ClassifyPostgreSqlException(const std::exception_ptr& exception) noexcept
        -> DependencyOutcome;

    class ObservedDbClient final : public drogon::orm::DbClient {
    public:
        explicit ObservedDbClient(drogon::orm::DbClientPtr delegate);

        [[nodiscard]] auto newTransaction(
            const std::function<void(bool)>& commit_callback = std::function<void(bool)>()
        ) -> std::shared_ptr<drogon::orm::Transaction> override;

        auto newTransactionAsync(
            const std::function<void(const std::shared_ptr<drogon::orm::Transaction>&)>& callback
        ) -> void override;

        [[nodiscard]] auto hasAvailableConnections() const noexcept -> bool override;
        auto setTimeout(double timeout) -> void override;
        auto closeAll() -> void override;

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
        ) -> void override;

        drogon::orm::DbClientPtr m_delegate;
    };

    [[nodiscard]] auto ObserveDbClient(drogon::orm::DbClientPtr client)
        -> drogon::orm::DbClientPtr;

} // namespace disk::metrics
