/**
 * @file ScheduledTasks.hpp
 * @brief 持久化周期任务播种器
 */

#pragma once

#include <atomic>
#include <chrono>
#include <expected>
#include <memory>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>
#include <trantor/net/EventLoop.h>

#include "services/StorageJobRepository.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/Singleton.hpp"

namespace disk::services {

    inline constexpr double kPeriodicSeedIntervalSeconds = 60.0;

    struct PeriodicSeedPlan {
        std::string hourly_scan_id;
        std::string daily_scan_id;
        std::vector<disk::jobs::NewStorageJob> jobs;
    };

    struct PeriodicSeedResult {
        size_t attempted{ 0 };
        size_t enqueued{ 0 };
        size_t deduplicated{ 0 };
    };

    [[nodiscard]]
    auto BuildPeriodicSeedPlan(std::chrono::system_clock::time_point now)
        -> std::expected<PeriodicSeedPlan, std::string>;

    class ScheduledTasks final : public disk::utils::Singleton<ScheduledTasks>,
                                 public std::enable_shared_from_this<ScheduledTasks> {
        friend class disk::utils::Singleton<ScheduledTasks>;

    public:
        static auto Initialize(
            drogon::orm::DbClientPtr db_client,
            std::string instance_id
        ) -> void;

        static auto Register() -> void;

        static auto Stop() -> void;

        [[nodiscard]]
        auto SeedOnce(std::chrono::system_clock::time_point now) -> drogon::Task<Result<PeriodicSeedResult>>;

        [[nodiscard]]
        auto IsDrained() const noexcept -> bool;

        ~ScheduledTasks() = default;
        ScheduledTasks(const ScheduledTasks&) = delete;
        auto operator=(const ScheduledTasks&) -> ScheduledTasks& = delete;
        ScheduledTasks(ScheduledTasks&&) = delete;
        auto operator=(ScheduledTasks&&) -> ScheduledTasks& = delete;

    private:
        ScheduledTasks() = default;

        auto TriggerSeed() -> void;

        std::shared_ptr<disk::jobs::StorageJobRepository> m_repository;
        trantor::EventLoop* m_loop{};
        std::atomic<trantor::TimerId> m_seed_timer{ trantor::InvalidTimerId };
        std::atomic_bool m_started{ false };
        std::atomic_bool m_accepting{ false };
        std::atomic_bool m_seed_inflight{ false };
    };

} // namespace disk::services
