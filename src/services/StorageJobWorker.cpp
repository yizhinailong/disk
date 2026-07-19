/**
 * @file StorageJobWorker.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 持久存储任务 Worker 实现
 *
 * @copyright Copyright (c) 2026
 */

#include "StorageJobWorker.hpp"

#include <algorithm>
#include <atomic>
#include <expected>
#include <memory>
#include <stdexcept>
#include <utility>

#include <drogon/drogon.h>

#include "storage/UploadStagingStorage.hpp"
#include "utils/LogHelper.hpp"

namespace disk::jobs {
    namespace {
        struct LeaseHeartbeatState {
            std::atomic_bool active{ true };
            std::atomic_bool renewal_inflight{ false };
            std::atomic_bool ownership_lost{ false };
        };

        [[nodiscard]] auto PermanentFailure(std::string error) -> JobExecutionResult {
            return JobExecutionResult{
                .succeeded = false,
                .retryable = false,
                .error = std::move(error),
            };
        }

        [[nodiscard]] auto RetryableFailure(std::string error) -> JobExecutionResult {
            return JobExecutionResult{
                .succeeded = false,
                .retryable = true,
                .error = std::move(error),
            };
        }

        [[nodiscard]] auto ParseStagingSession(const StorageJob& job)
            -> std::expected<disk::storage::UploadStagingSession, std::string> {
            if (!job.payload.isObject()) {
                return std::unexpected("staging_cleanup payload must be a JSON object");
            }
            for (const auto* field : { "upload_id", "backend", "prefix" }) {
                if (!job.payload.isMember(field) || !job.payload[field].isString() ||
                    job.payload[field].asString().empty()) {
                    return std::unexpected(
                        std::string("staging_cleanup payload requires non-empty string field: ") + field
                    );
                }
            }

            const auto upload_id = job.payload["upload_id"].asString();
            if (upload_id != job.aggregate_id) {
                return std::unexpected("staging_cleanup aggregate_id does not match upload_id");
            }

            const auto backend = disk::storage::ParseUploadStagingBackend(
                job.payload["backend"].asString()
            );
            if (!backend.has_value()) {
                return std::unexpected("staging_cleanup payload contains an unsupported backend");
            }

            return disk::storage::UploadStagingSession{
                .upload_id = upload_id,
                .backend = backend.value(),
                .prefix = job.payload["prefix"].asString(),
            };
        }

        [[nodiscard]] auto MixRetrySeed(uint64_t value) noexcept -> uint64_t {
            value += 0x9e3779b97f4a7c15ULL;
            value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
            value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
            return value ^ (value >> 31U);
        }
    } // namespace

    StorageJobWorker::StorageJobWorker(
        drogon::orm::DbClientPtr db_client,
        disk::storage::UploadStagingStorage* staging_storage,
        std::string instance_id,
        StorageJobWorkerOptions options
    ) : m_db_client(std::move(db_client)),
        m_staging_storage(staging_storage),
        m_instance_id(std::move(instance_id)),
        m_options(options) {
        if (m_instance_id.empty() || m_instance_id.size() > 128) {
            throw std::invalid_argument("Storage job worker instance ID must contain 1 to 128 characters");
        }
        if (m_options.batch_size == 0 || m_options.batch_size > 1000) {
            throw std::invalid_argument("Storage job worker batch size must be in range 1-1000");
        }
        if (m_options.lease_duration_seconds < 3) {
            throw std::invalid_argument("Storage job worker lease duration must be at least 3 seconds");
        }
        if (m_options.retry_base_seconds == 0 ||
            m_options.retry_cap_seconds < m_options.retry_base_seconds) {
            throw std::invalid_argument("Storage job worker retry delay bounds are invalid");
        }
    }

    auto StorageJobWorker::ComputeRetryDelaySeconds(
        uint64_t job_id,
        uint32_t attempts,
        uint32_t base_seconds,
        uint32_t cap_seconds
    ) -> uint32_t {
        if (base_seconds == 0 || cap_seconds < base_seconds) {
            throw std::invalid_argument("Storage job retry delay bounds are invalid");
        }

        const auto exponent = std::min<uint32_t>(attempts > 0 ? attempts - 1 : 0, 31);
        uint64_t delay = base_seconds;
        for (uint32_t index = 0; index < exponent && delay < cap_seconds; ++index) {
            delay = std::min<uint64_t>(delay * 2, cap_seconds);
        }

        const auto remaining = static_cast<uint64_t>(cap_seconds) - delay;
        const auto jitter_window = std::min(delay / 4, remaining);
        if (jitter_window > 0) {
            const auto seed = job_id ^ (static_cast<uint64_t>(attempts) << 32U);
            delay += MixRetrySeed(seed) % (jitter_window + 1);
        }
        return static_cast<uint32_t>(delay);
    }

    auto StorageJobWorker::ExecuteJob(const StorageJob& job) const
        -> drogon::Task<JobExecutionResult> {
        if (job.job_type != kStagingCleanupJobType) {
            co_return PermanentFailure("Unsupported storage job type: " + job.job_type);
        }

        auto session = ParseStagingSession(job);
        if (!session) {
            co_return PermanentFailure(session.error());
        }
        if (m_staging_storage == nullptr) {
            co_return RetryableFailure("Upload staging storage is not configured");
        }

        auto cleanup_result = co_await m_staging_storage->CleanupSession(session.value());
        if (cleanup_result) {
            co_return JobExecutionResult{ .succeeded = true };
        }

        const auto& error = cleanup_result.error();
        const auto retryable =
            error.code != ErrorCode::InvalidParameter &&
            error.code != ErrorCode::ValidationFailed;
        co_return JobExecutionResult{
            .succeeded = false,
            .retryable = retryable,
            .error = error.message.empty() ? "staging_cleanup failed with code " + std::to_string(error.CodeInt()) : error.message,
        };
    }

    auto StorageJobWorker::ProcessClaimedJob(const StorageJob& job) const
        -> drogon::Task<PersistDisposition> {
        StorageJobRepository repository(m_db_client);
        try {
            const auto renewed = co_await repository.RenewLease(
                job.id,
                m_instance_id,
                m_options.lease_duration_seconds
            );
            if (!renewed) {
                co_return PersistDisposition::OwnershipLost;
            }
        } catch (const std::exception& error) {
            Logger::Warn() << "Storage job lease preflight failed: job_id=" << job.id
                           << ", instance_id=" << m_instance_id << ", error=" << error.what();
            co_return PersistDisposition::OwnershipLost;
        }

        auto heartbeat_state = std::make_shared<LeaseHeartbeatState>();
        auto* loop = drogon::app().getLoop();
        trantor::TimerId heartbeat_timer = 0;
        if (loop != nullptr) {
            const auto heartbeat_interval =
                std::max(1.0, static_cast<double>(m_options.lease_duration_seconds) / 3.0);
            const auto db_client = m_db_client;
            const auto instance_id = m_instance_id;
            const auto lease_duration_seconds = m_options.lease_duration_seconds;
            const auto job_id = job.id;
            heartbeat_timer = loop->runEvery(
                heartbeat_interval,
                [heartbeat_state, db_client, instance_id, lease_duration_seconds, job_id]() {
                    if (!heartbeat_state->active.load() ||
                        heartbeat_state->renewal_inflight.exchange(true)) {
                        return;
                    }
                    drogon::async_run(
                        [heartbeat_state, db_client, instance_id, lease_duration_seconds, job_id]()
                            -> drogon::Task<void> {
                            try {
                                StorageJobRepository repository(db_client);
                                const auto renewed = co_await repository.RenewLease(
                                    job_id,
                                    instance_id,
                                    lease_duration_seconds
                                );
                                if (!renewed) {
                                    heartbeat_state->ownership_lost.store(true);
                                    heartbeat_state->active.store(false);
                                }
                            } catch (const std::exception& error) {
                                Logger::Warn() << "Storage job lease heartbeat failed: job_id="
                                               << job_id << ", instance_id=" << instance_id
                                               << ", error=" << error.what();
                            }
                            heartbeat_state->renewal_inflight.store(false);
                        }
                    );
                }
            );
        }

        JobExecutionResult execution;
        try {
            execution = co_await ExecuteJob(job);
        } catch (const std::exception& error) {
            execution = RetryableFailure(std::string("Storage job handler threw: ") + error.what());
        }

        heartbeat_state->active.store(false);
        if (loop != nullptr && heartbeat_timer != 0) {
            loop->invalidateTimer(heartbeat_timer);
        }
        if (heartbeat_state->ownership_lost.load()) {
            co_return PersistDisposition::OwnershipLost;
        }

        try {
            if (execution.succeeded) {
                const auto persisted = co_await repository.MarkSucceeded(job.id, m_instance_id);
                co_return persisted ? PersistDisposition::Succeeded : PersistDisposition::OwnershipLost;
            }

            const auto retry_delay = ComputeRetryDelaySeconds(
                job.id,
                job.attempts,
                m_options.retry_base_seconds,
                m_options.retry_cap_seconds
            );
            auto persisted = co_await repository.MarkFailed(
                job.id,
                m_instance_id,
                execution.error,
                execution.retryable,
                retry_delay
            );
            if (!persisted.has_value()) {
                co_return PersistDisposition::OwnershipLost;
            }
            co_return persisted.value() == StorageJobStatus::Retry ? PersistDisposition::Retried : PersistDisposition::DeadLettered;
        } catch (const std::exception& error) {
            Logger::Warn() << "Storage job result persistence failed: job_id=" << job.id
                           << ", instance_id=" << m_instance_id << ", error=" << error.what();
            co_return PersistDisposition::OwnershipLost;
        }
    }

    auto StorageJobWorker::RunOnce() const -> drogon::Task<Result<StorageJobRunResult>> {
        StorageJobRepository repository(m_db_client);
        std::vector<StorageJob> jobs;
        try {
            jobs = co_await repository.ClaimReadyBatch(
                m_instance_id,
                m_options.batch_size,
                m_options.lease_duration_seconds
            );
        } catch (const std::exception& error) {
            Logger::Error() << "Storage job claim failed: instance_id=" << m_instance_id
                            << ", error=" << error.what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to claim storage jobs")
            );
        }

        StorageJobRunResult result{ .claimed = jobs.size() };
        for (const auto& job : jobs) {
            const auto disposition = co_await ProcessClaimedJob(job);
            switch (disposition) {
                case PersistDisposition::Succeeded:
                    result.succeeded++;
                    break;
                case PersistDisposition::Retried:
                    result.retried++;
                    break;
                case PersistDisposition::DeadLettered:
                    result.dead_lettered++;
                    break;
                case PersistDisposition::OwnershipLost:
                    result.ownership_lost++;
                    break;
            }
        }

        co_return result;
    }

} // namespace disk::jobs
