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
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <utility>

#include <drogon/drogon.h>

#include "services/StorageJobContract.hpp"
#include "services/StorageReconciliationService.hpp"
#include "services/TransactionRunner.hpp"
#include "services/TrashService.hpp"
#include "services/UploadLifecycleService.hpp"
#include "storage/IBlobStore.hpp"
#include "storage/MultipartUploadRecovery.hpp"
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

        [[nodiscard]] auto ParseMultipartUpload(const StorageJob& job)
            -> std::expected<disk::storage::MultipartUploadDescriptor, std::string> {
            if (!job.payload.isObject()) {
                return std::unexpected("multipart_abort payload must be a JSON object");
            }
            for (const auto* field : { "backend", "key", "upload_id" }) {
                if (!job.payload.isMember(field) || !job.payload[field].isString() ||
                    job.payload[field].asString().empty()) {
                    return std::unexpected(
                        std::string("multipart_abort payload requires non-empty string field: ") + field
                    );
                }
            }
            if (job.payload["backend"].asString() != "s3") {
                return std::unexpected("multipart_abort payload contains an unsupported backend");
            }

            disk::storage::MultipartUploadDescriptor descriptor{
                .key = job.payload["key"].asString(),
                .upload_id = job.payload["upload_id"].asString(),
            };
            const auto recovery_id = disk::storage::BuildMultipartUploadRecoveryId(descriptor);
            if (job.aggregate_id != recovery_id) {
                return std::unexpected("multipart_abort aggregate_id does not match payload");
            }
            if (job.dedupe_key != disk::storage::BuildMultipartUploadRecoveryDedupeKey(descriptor)) {
                return std::unexpected("multipart_abort dedupe_key does not match payload");
            }
            return descriptor;
        }

        struct BlobGcCandidate {
            uint64_t content_id{ 0 };
            std::filesystem::path storage_path;
        };

        [[nodiscard]] auto ParseBlobGcCandidate(const StorageJob& job)
            -> std::expected<BlobGcCandidate, std::string> {
            if (!job.payload.isObject()) {
                return std::unexpected("blob_gc payload must be a JSON object");
            }
            if (!job.payload.isMember("content_id") || !job.payload["content_id"].isUInt64() ||
                job.payload["content_id"].asUInt64() == 0) {
                return std::unexpected("blob_gc payload requires a positive uint64 content_id");
            }
            if (!job.payload.isMember("storage_path") ||
                !job.payload["storage_path"].isString() ||
                job.payload["storage_path"].asString().empty()) {
                return std::unexpected("blob_gc payload requires a non-empty storage_path");
            }

            const auto content_id = job.payload["content_id"].asUInt64();
            if (job.aggregate_id != std::to_string(content_id)) {
                return std::unexpected("blob_gc aggregate_id does not match content_id");
            }

            return BlobGcCandidate{
                .content_id = content_id,
                .storage_path = std::filesystem::path(job.payload["storage_path"].asString()),
            };
        }

        auto RollbackQuietly(const std::shared_ptr<drogon::orm::Transaction>& transaction) -> void {
            if (transaction == nullptr) {
                return;
            }
            try {
                transaction->rollback();
            } catch (const std::exception& error) {
                Logger::Warn() << "Blob GC transaction rollback failed: " << error.what();
            }
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
        disk::storage::IBlobStore* blob_store,
        std::string instance_id,
        StorageJobWorkerOptions options,
        disk::storage::IMultipartUploadCleaner* multipart_upload_cleaner
    ) : m_db_client(std::move(db_client)),
        m_staging_storage(staging_storage),
        m_blob_store(blob_store),
        m_multipart_upload_cleaner(multipart_upload_cleaner),
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

        m_handlers.emplace(
            std::string(kStagingCleanupJobType),
            &StorageJobWorker::ExecuteStagingCleanup
        );
        m_handlers.emplace(
            std::string(kMultipartAbortJobType),
            &StorageJobWorker::ExecuteMultipartAbort
        );
        m_handlers.emplace(std::string(kBlobGcJobType), &StorageJobWorker::ExecuteBlobGc);
        m_handlers.emplace(
            std::string(kExpireUploadsJobType),
            &StorageJobWorker::ExecuteExpireUploads
        );
        m_handlers.emplace(
            std::string(kExpireTrashJobType),
            &StorageJobWorker::ExecuteExpireTrash
        );
        m_handlers.emplace(
            std::string(kStorageReconcileJobType),
            &StorageJobWorker::ExecuteStorageReconcile
        );
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
        const auto handler = m_handlers.find(job.job_type);
        if (handler == m_handlers.end()) {
            co_return PermanentFailure("Unsupported storage job type: " + job.job_type);
        }
        co_return co_await (this->*(handler->second))(job);
    }

    auto StorageJobWorker::ExecuteStagingCleanup(const StorageJob& job) const
        -> drogon::Task<JobExecutionResult> {
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

    auto StorageJobWorker::ExecuteMultipartAbort(const StorageJob& job) const
        -> drogon::Task<JobExecutionResult> {
        auto descriptor = ParseMultipartUpload(job);
        if (!descriptor) {
            co_return PermanentFailure(descriptor.error());
        }
        if (m_multipart_upload_cleaner == nullptr) {
            co_return RetryableFailure("Multipart upload cleaner is not configured");
        }

        auto abort_result = co_await m_multipart_upload_cleaner->AbortMultipartUpload(
            descriptor.value()
        );
        if (abort_result) {
            co_return JobExecutionResult{ .succeeded = true };
        }
        const auto& error = abort_result.error();
        const auto retryable =
            error.code != ErrorCode::InvalidParameter &&
            error.code != ErrorCode::ValidationFailed;
        co_return JobExecutionResult{
            .succeeded = false,
            .retryable = retryable,
            .error = error.message.empty() ? "multipart_abort failed" : error.message,
        };
    }

    auto StorageJobWorker::ExecuteBlobGc(const StorageJob& job) const
        -> drogon::Task<JobExecutionResult> {
        auto candidate = ParseBlobGcCandidate(job);
        if (!candidate) {
            co_return PermanentFailure(candidate.error());
        }
        if (m_db_client == nullptr) {
            co_return RetryableFailure("Blob GC database is not configured");
        }
        if (m_blob_store == nullptr) {
            co_return RetryableFailure("Blob store is not configured");
        }

        std::shared_ptr<drogon::orm::Transaction> transaction;
        try {
            transaction = co_await disk::file::TransactionRunner::Begin(m_db_client);
            StorageJobRepository repository(m_db_client);
            const auto complete_transaction = [&]() -> drogon::Task<JobExecutionResult> {
                co_await transaction->execSqlCoro(
                    "UPDATE storage_reconciliation_findings SET " "  resolved_at = COALESCE(resolved_at, NOW()), last_seen_at = NOW() " "WHERE finding_type = 'zero_reference_content' " "  AND resource_id = $1 AND resolved_at IS NULL",
                    std::to_string(candidate->content_id)
                );
                const auto persisted = co_await repository.MarkSucceeded(
                    transaction,
                    job.id,
                    m_instance_id
                );
                if (!persisted) {
                    RollbackQuietly(transaction);
                    co_return RetryableFailure("blob_gc ownership changed before commit");
                }
                auto commit_result = co_await disk::file::TransactionRunner::Commit(transaction);
                if (!commit_result) {
                    co_return RetryableFailure("blob_gc transaction commit failed");
                }
                co_return JobExecutionResult{
                    .succeeded = true,
                    .outcome_persisted = true,
                };
            };

            auto content = co_await transaction->execSqlCoro(
                "SELECT content.storage_path, content.ref_count, " "  EXISTS (SELECT 1 FROM files WHERE content_id = content.id) AS has_file_ref, " "  EXISTS (SELECT 1 FROM trash WHERE content_id = content.id) AS has_trash_ref " "FROM file_contents AS content WHERE content.id = $1 FOR UPDATE OF content",
                static_cast<int64_t>(candidate->content_id)
            );
            if (content.empty()) {
                co_return co_await complete_transaction();
            }

            const auto persisted_path = content[0]["storage_path"].as<std::string>();
            if (persisted_path != candidate->storage_path.string()) {
                RollbackQuietly(transaction);
                co_return PermanentFailure("blob_gc storage_path does not match file_contents");
            }

            const auto ref_count = content[0]["ref_count"].as<int32_t>();
            if (ref_count > 0) {
                co_return co_await complete_transaction();
            }
            if (ref_count < 0 || content[0]["has_file_ref"].as<bool>() ||
                content[0]["has_trash_ref"].as<bool>()) {
                RollbackQuietly(transaction);
                co_return PermanentFailure("blob_gc found inconsistent content references");
            }

            auto delete_result = co_await m_blob_store->DeleteBlob(candidate->storage_path);
            if (!delete_result) {
                RollbackQuietly(transaction);
                const auto& error = delete_result.error();
                const auto retryable =
                    error.code != ErrorCode::InvalidParameter &&
                    error.code != ErrorCode::ValidationFailed;
                co_return JobExecutionResult{
                    .succeeded = false,
                    .retryable = retryable,
                    .error = error.message.empty() ? "blob_gc storage deletion failed" : error.message,
                };
            }

            auto deleted = co_await transaction->execSqlCoro(
                "DELETE FROM file_contents AS content " "WHERE content.id = $1 AND content.ref_count = 0 " "  AND NOT EXISTS (SELECT 1 FROM files WHERE content_id = content.id) " "  AND NOT EXISTS (SELECT 1 FROM trash WHERE content_id = content.id) " "RETURNING content.id",
                static_cast<int64_t>(candidate->content_id)
            );
            if (deleted.size() != 1) {
                RollbackQuietly(transaction);
                co_return RetryableFailure("blob_gc content row changed before deletion");
            }

            co_return co_await complete_transaction();
        } catch (const drogon::orm::DrogonDbException& error) {
            RollbackQuietly(transaction);
            co_return RetryableFailure(std::string("blob_gc database failure: ") + error.base().what());
        } catch (const std::exception& error) {
            RollbackQuietly(transaction);
            co_return RetryableFailure(std::string("blob_gc handler failure: ") + error.what());
        }
    }

    auto StorageJobWorker::ExecuteExpireUploads(const StorageJob& job) const
        -> drogon::Task<JobExecutionResult> {
        auto request = ParseExpireUploadsJob(job);
        if (!request) {
            co_return PermanentFailure(request.error());
        }
        if (m_db_client == nullptr) {
            co_return RetryableFailure("Upload expiration database is not configured");
        }

        disk::upload::UploadLifecycleService lifecycle_service(
            m_db_client,
            nullptr,
            m_staging_storage,
            m_blob_store
        );
        auto expiration = co_await lifecycle_service.ExpireInProgressUploads(request->limit);
        if (!expiration) {
            const auto& error = expiration.error();
            const auto retryable =
                error.code != ErrorCode::InvalidParameter &&
                error.code != ErrorCode::ValidationFailed;
            co_return JobExecutionResult{
                .succeeded = false,
                .retryable = retryable,
                .error = error.message.empty() ? "expire_uploads failed" : error.message,
            };
        }

        bool continuation_enqueued = false;
        if (disk::upload::ShouldContinueExpirationScan(
                expiration->candidates,
                request->limit
            )) {
            auto next_request = request.value();
            ++next_request.page;
            auto next_job = BuildExpireUploadsJob(next_request);
            if (!next_job) {
                co_return PermanentFailure(next_job.error());
            }
            StorageJobRepository repository(m_db_client);
            continuation_enqueued = co_await repository.Enqueue(next_job.value());
        }

        Logger::Info() << "Storage job page completed: instance_id=" << m_instance_id
                       << ", job_type=" << kExpireUploadsJobType
                       << ", scan_id=" << request->scan_id << ", page=" << request->page
                       << ", candidates=" << expiration->candidates
                       << ", expired=" << expiration->expired
                       << ", continuation_enqueued=" << continuation_enqueued;
        co_return JobExecutionResult{ .succeeded = true };
    }

    auto StorageJobWorker::ExecuteExpireTrash(const StorageJob& job) const
        -> drogon::Task<JobExecutionResult> {
        auto request = ParseExpireTrashJob(job);
        if (!request) {
            co_return PermanentFailure(request.error());
        }
        if (m_db_client == nullptr) {
            co_return RetryableFailure("Trash expiration database is not configured");
        }

        disk::trash::TrashService trash_service(m_db_client);
        auto page = co_await trash_service.CleanupExpiredTrashPage(
            request->after_id,
            request->limit
        );
        if (!page) {
            const auto& error = page.error();
            const auto retryable =
                error.code != ErrorCode::InvalidParameter &&
                error.code != ErrorCode::ValidationFailed;
            co_return JobExecutionResult{
                .succeeded = false,
                .retryable = retryable,
                .error = error.message.empty() ? "expire_trash failed" : error.message,
            };
        }

        bool continuation_enqueued = false;
        if (page->has_more) {
            if (page->next_after_id <= request->after_id) {
                co_return PermanentFailure("expire_trash continuation cursor did not advance");
            }
            auto next_request = request.value();
            next_request.after_id = page->next_after_id;
            auto next_job = BuildExpireTrashJob(next_request);
            if (!next_job) {
                co_return PermanentFailure(next_job.error());
            }
            StorageJobRepository repository(m_db_client);
            continuation_enqueued = co_await repository.Enqueue(next_job.value());
        }

        Logger::Info() << "Storage job page completed: instance_id=" << m_instance_id
                       << ", job_type=" << kExpireTrashJobType
                       << ", scan_id=" << request->scan_id
                       << ", after_id=" << request->after_id
                       << ", candidates=" << page->candidates
                       << ", deleted=" << page->deleted
                       << ", next_after_id=" << page->next_after_id
                       << ", continuation_enqueued=" << continuation_enqueued;
        co_return JobExecutionResult{ .succeeded = true };
    }

    auto StorageJobWorker::ExecuteStorageReconcile(const StorageJob& job) const
        -> drogon::Task<JobExecutionResult> {
        auto request = ParseStorageReconcileJob(job);
        if (!request) {
            co_return PermanentFailure(request.error());
        }
        if (m_db_client == nullptr) {
            co_return RetryableFailure("Storage reconciliation database is not configured");
        }

        disk::reconciliation::StorageReconciliationService reconciliation_service(
            m_db_client,
            m_staging_storage,
            m_blob_store
        );
        auto page = co_await reconciliation_service.RunPage(request.value());
        if (!page) {
            const auto& error = page.error();
            const auto retryable =
                error.code != ErrorCode::InvalidParameter &&
                error.code != ErrorCode::ValidationFailed;
            co_return JobExecutionResult{
                .succeeded = false,
                .retryable = retryable,
                .error = error.message.empty() ? "storage_reconcile failed" : error.message,
            };
        }

        bool continuation_enqueued = false;
        if (page->has_more) {
            auto next_request = request.value();
            next_request.after_id = page->next_after_id;
            next_request.continuation_token = page->next_continuation_token;
            const auto is_database_scope =
                request->scope == disk::reconciliation::ReconciliationScope::Contents ||
                request->scope == disk::reconciliation::ReconciliationScope::Users;
            const auto cursor_advanced = is_database_scope ? next_request.after_id > request->after_id : !next_request.continuation_token.empty() && next_request.continuation_token != request->continuation_token;
            if (!cursor_advanced) {
                co_return PermanentFailure("storage_reconcile continuation cursor did not advance");
            }

            auto next_job = BuildStorageReconcileJob(next_request);
            if (!next_job) {
                co_return PermanentFailure(next_job.error());
            }
            StorageJobRepository repository(m_db_client);
            continuation_enqueued = co_await repository.Enqueue(next_job.value());
        }

        Logger::Info() << "Storage job page completed: instance_id=" << m_instance_id
                       << ", job_type=" << kStorageReconcileJobType
                       << ", scan_id=" << request->scan_id
                       << ", scope=" << disk::reconciliation::ToStorageValue(request->scope)
                       << ", inspected=" << page->inspected
                       << ", findings=" << page->findings_recorded
                       << ", repairs_enqueued=" << page->repairs_enqueued
                       << ", continuation_enqueued=" << continuation_enqueued;
        co_return JobExecutionResult{ .succeeded = true };
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
        if (execution.outcome_persisted) {
            co_return PersistDisposition::Succeeded;
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
