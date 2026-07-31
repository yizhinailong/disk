/**
 * @file ScheduledTasks.cpp
 * @brief 持久化周期任务播种器实现
 */

#include "services/ScheduledTasks.hpp"

#include <array>
#include <ctime>
#include <stdexcept>
#include <utility>

#include <drogon/drogon.h>

#include "services/StorageJobContract.hpp"
#include "services/StorageReconciliationService.hpp"
#include "utils/LogHelper.hpp"

namespace disk::services {
    namespace {
        class InflightReset final {
        public:
            explicit InflightReset(std::atomic_bool& inflight) : m_inflight(inflight) {}

            ~InflightReset() { m_inflight.store(false); }

            InflightReset(const InflightReset&) = delete;
            auto operator=(const InflightReset&) -> InflightReset& = delete;

        private:
            std::atomic_bool& m_inflight;
        };

        [[nodiscard]] auto ToUtc(std::time_t timestamp) -> std::tm {
            std::tm utc{};
#ifdef _WIN32
            if (gmtime_s(&utc, &timestamp) != 0) {
#else
            if (gmtime_r(&timestamp, &utc) == nullptr) {
#endif
                throw std::runtime_error("Failed to convert periodic scan time to UTC");
            }
            return utc;
        }

        [[nodiscard]] auto FormatUtc(const std::tm& utc, const char* pattern) -> std::string {
            std::array<char, 32> buffer{};
            if (std::strftime(buffer.data(), buffer.size(), pattern, &utc) == 0) {
                throw std::runtime_error("Failed to format periodic scan time");
            }
            return buffer.data();
        }

        auto AppendJob(
            PeriodicSeedPlan& plan,
            std::expected<disk::jobs::NewStorageJob, std::string> job
        ) -> std::expected<void, std::string> {
            if (!job) {
                return std::unexpected(job.error());
            }
            plan.jobs.push_back(std::move(job.value()));
            return {};
        }
    } // namespace

    auto BuildPeriodicSeedPlan(std::chrono::system_clock::time_point now)
        -> std::expected<PeriodicSeedPlan, std::string> {
        try {
            const auto utc = ToUtc(std::chrono::system_clock::to_time_t(now));
            PeriodicSeedPlan plan{
                .hourly_scan_id = FormatUtc(utc, "%Y%m%dT%HZ"),
                .daily_scan_id = FormatUtc(utc, "%Y%m%d"),
            };
            plan.jobs.reserve(6);

            if (auto result = AppendJob(
                    plan,
                    disk::jobs::BuildExpireUploadsJob(disk::jobs::ExpireUploadsPageRequest{
                        .scan_id = plan.hourly_scan_id,
                    })
                );
                !result) {
                return std::unexpected(result.error());
            }
            if (auto result = AppendJob(
                    plan,
                    disk::jobs::BuildExpireTrashJob(disk::jobs::ExpireTrashPageRequest{
                        .scan_id = plan.hourly_scan_id,
                    })
                );
                !result) {
                return std::unexpected(result.error());
            }

            constexpr std::array database_scopes{
                disk::reconciliation::ReconciliationScope::Contents,
                disk::reconciliation::ReconciliationScope::Users,
            };
            for (const auto scope : database_scopes) {
                if (auto result = AppendJob(
                        plan,
                        disk::jobs::BuildStorageReconcileJob(
                            disk::reconciliation::ReconciliationPageRequest{
                                .scan_id = plan.daily_scan_id,
                                .scope = scope,
                                .limit = disk::reconciliation::kMaxDatabaseReconciliationPageSize,
                            }
                        )
                    );
                    !result) {
                    return std::unexpected(result.error());
                }
            }

            constexpr std::array object_scopes{
                disk::reconciliation::ReconciliationScope::Staging,
                disk::reconciliation::ReconciliationScope::Final,
            };
            for (const auto scope : object_scopes) {
                if (auto result = AppendJob(
                        plan,
                        disk::jobs::BuildStorageReconcileJob(
                            disk::reconciliation::ReconciliationPageRequest{
                                .scan_id = plan.daily_scan_id,
                                .scope = scope,
                                .limit = disk::reconciliation::kMaxObjectReconciliationPageSize,
                            }
                        )
                    );
                    !result) {
                    return std::unexpected(result.error());
                }
            }
            return plan;
        } catch (const std::exception& error) {
            return std::unexpected(error.what());
        }
    }

    auto ScheduledTasks::Initialize(drogon::orm::DbClientPtr db_client) -> void {
        if (db_client == nullptr) {
            throw std::invalid_argument("Periodic task database client is required");
        }

        auto instance = GetInstance();
        if (instance->m_repository != nullptr) {
            return;
        }
        instance->m_repository =
            std::make_shared<disk::jobs::StorageJobRepository>(std::move(db_client));
        instance->m_accepting.store(true);
    }

    auto ScheduledTasks::Register() -> void {
        auto instance = GetInstance();
        if (instance->m_repository == nullptr) {
            throw std::logic_error("Periodic tasks must be initialized before registration");
        }

        bool expected = false;
        if (!instance->m_started.compare_exchange_strong(expected, true)) {
            return;
        }

        instance->m_loop = drogon::app().getLoop();
        const auto weak_instance = std::weak_ptr<ScheduledTasks>(instance);
        instance->m_seed_timer.store(instance->m_loop->runEvery(
            kPeriodicSeedIntervalSeconds,
            [weak_instance]() {
                if (const auto current = weak_instance.lock(); current != nullptr) {
                    current->TriggerSeed();
                }
            }
        ));
        instance->m_loop->queueInLoop([weak_instance]() {
            if (const auto current = weak_instance.lock(); current != nullptr) {
                current->TriggerSeed();
            }
        });

        Logger::Info(utils::LogContext{ .operation = "storage_job_scheduler" })
            << "Periodic storage job seeder started: interval_seconds="
            << kPeriodicSeedIntervalSeconds;
    }

    auto ScheduledTasks::Stop() -> void {
        auto instance = GetInstance();
        if (!instance->m_accepting.exchange(false)) {
            return;
        }

        const auto timer = instance->m_seed_timer.exchange(trantor::InvalidTimerId);
        if (instance->m_loop != nullptr && timer != trantor::InvalidTimerId) {
            instance->m_loop->queueInLoop(
                [loop = instance->m_loop, timer]() { loop->invalidateTimer(timer); }
            );
        }
        Logger::Info(utils::LogContext{ .operation = "storage_job_scheduler" })
            << "Periodic storage job seeder stopped: in_flight="
            << instance->m_seed_inflight.load();
    }

    auto ScheduledTasks::SeedOnce(std::chrono::system_clock::time_point now)
        -> drogon::Task<Result<PeriodicSeedResult>> {
        if (!m_accepting.load() || m_seed_inflight.exchange(true)) {
            co_return PeriodicSeedResult{};
        }
        InflightReset reset(m_seed_inflight);

        auto plan = BuildPeriodicSeedPlan(now);
        if (!plan) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to build periodic storage jobs")
            );
        }

        PeriodicSeedResult result{ .attempted = plan->jobs.size() };
        try {
            for (const auto& job : plan->jobs) {
                if (co_await m_repository->Enqueue(job)) {
                    result.enqueued++;
                } else {
                    result.deduplicated++;
                }
            }
            co_return result;
        } catch (const std::exception&) {
            Logger::Error(utils::LogContext{ .operation = "storage_job_seed" })
                << "Periodic storage job seed failed";
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to seed periodic storage jobs")
            );
        }
    }

    auto ScheduledTasks::IsDrained() const noexcept -> bool {
        return !m_seed_inflight.load();
    }

    auto ScheduledTasks::TriggerSeed() -> void {
        const auto weak_self = weak_from_this();
        drogon::async_run([weak_self]() -> drogon::Task<void> {
            if (const auto self = weak_self.lock(); self != nullptr) {
                auto result = co_await self->SeedOnce(std::chrono::system_clock::now());
                if (!result) {
                    Logger::Error(utils::LogContext{ .operation = "storage_job_seed" })
                        << "Periodic storage job seed cycle failed";
                } else if (result->attempted > 0) {
                    Logger::Info(utils::LogContext{ .operation = "storage_job_seed" })
                        << "Periodic storage job seed cycle completed: attempted="
                        << result->attempted
                        << ", enqueued=" << result->enqueued
                        << ", deduplicated=" << result->deduplicated;
                }
            }
        });
    }

} // namespace disk::services
