#include "services/StorageJobWorker.hpp"

#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "services/StorageJobContract.hpp"
#include "storage/MultipartUploadRecovery.hpp"
#include "storage/UploadStagingStorage.hpp"
#include "utils/LogHelper.hpp"

namespace disk::jobs {
    namespace {
        class ScopedLogCapture {
        public:
            ScopedLogCapture()
                : m_previous_logger(spdlog::default_logger()),
                  m_sink(std::make_shared<spdlog::sinks::ostream_sink_mt>(m_output)),
                  m_logger(std::make_shared<spdlog::logger>("storage-job-worker-test", m_sink)) {
                disk::utils::Logger::ApplyStructuredFormatter(m_logger);
                spdlog::set_default_logger(m_logger);
            }

            ~ScopedLogCapture() {
                spdlog::set_default_logger(m_previous_logger);
            }

            ScopedLogCapture(const ScopedLogCapture&) = delete;
            auto operator=(const ScopedLogCapture&) -> ScopedLogCapture& = delete;

            [[nodiscard]] auto Text() -> std::string {
                m_logger->flush();
                return m_output.str();
            }

        private:
            std::ostringstream m_output;
            std::shared_ptr<spdlog::logger> m_previous_logger;
            std::shared_ptr<spdlog::sinks::ostream_sink_mt> m_sink;
            std::shared_ptr<spdlog::logger> m_logger;
        };

        TEST(StorageJobWorkerOptionsTest, ClaimBatchDoesNotExceedConcurrency) {
            EXPECT_EQ(EffectiveWorkerClaimBatchSize(20, 1), 1U);
            EXPECT_EQ(EffectiveWorkerClaimBatchSize(20, 4), 4U);
            EXPECT_EQ(EffectiveWorkerClaimBatchSize(2, 4), 2U);
        }

        class RecordingStagingStorage final : public disk::storage::UploadStagingStorage {
        public:
            auto EnsureUploadSession(const disk::storage::UploadStagingSession&)
                -> drogon::Task<Result<void>> override {
                co_return {};
            }

            auto WriteChunk(
                const disk::storage::UploadStagingSession&,
                uint32_t,
                const std::string&,
                std::string
            ) -> drogon::Task<Result<disk::storage::UploadStagingChunk>> override {
                co_return std::unexpected(ErrorInfo(ErrorCode::InternalError));
            }

            auto AssembleChunks(
                const disk::storage::UploadStagingSession&,
                uint64_t,
                uint32_t,
                const std::vector<disk::storage::UploadStagingChunk>&
            ) -> drogon::Task<Result<disk::storage::UploadStagingAssembly>> override {
                co_return std::unexpected(ErrorInfo(ErrorCode::InternalError));
            }

            auto DiscardAssembly(
                const disk::storage::UploadStagingSession&,
                const disk::storage::UploadStagingAssembly&
            ) -> drogon::Task<Result<void>> override {
                co_return {};
            }

            auto CleanupSession(const disk::storage::UploadStagingSession& session)
                -> drogon::Task<Result<void>> override {
                cleaned_sessions.push_back(session);
                if (cleanup_error.has_value()) {
                    co_return std::unexpected(cleanup_error.value());
                }
                co_return {};
            }

            std::vector<disk::storage::UploadStagingSession> cleaned_sessions;
            std::optional<ErrorInfo> cleanup_error;
        };

        class RecordingMultipartUploadCleaner final
            : public disk::storage::IMultipartUploadCleaner {
        public:
            auto AbortMultipartUpload(
                const disk::storage::MultipartUploadDescriptor& descriptor
            ) -> drogon::Task<Result<void>> override {
                aborted.push_back(descriptor);
                if (error.has_value()) {
                    co_return std::unexpected(error.value());
                }
                co_return {};
            }

            std::vector<disk::storage::MultipartUploadDescriptor> aborted;
            std::optional<ErrorInfo> error;
        };

        [[nodiscard]] auto MakeCleanupJob() -> StorageJob {
            Json::Value payload(Json::objectValue);
            payload["upload_id"] = "upload-123";
            payload["backend"] = "s3";
            payload["prefix"] = "staging/upload-123";
            return StorageJob{
                .id = 42,
                .job_type = std::string(kStagingCleanupJobType),
                .aggregate_id = "upload-123",
                .dedupe_key = "staging-cleanup:upload-123",
                .payload = std::move(payload),
                .status = StorageJobStatus::Running,
                .attempts = 2,
                .max_attempts = 8,
                .locked_by = "worker-1",
            };
        }

        [[nodiscard]] auto MakeBlobGcJob() -> StorageJob {
            Json::Value payload(Json::objectValue);
            payload["content_id"] = Json::UInt64(91);
            payload["storage_path"] = "blobs/sha256/aa/blob";
            return StorageJob{
                .id = 43,
                .job_type = std::string(kBlobGcJobType),
                .aggregate_id = "91",
                .dedupe_key = "blob-gc:91",
                .payload = std::move(payload),
                .status = StorageJobStatus::Running,
                .attempts = 1,
                .max_attempts = 8,
                .locked_by = "worker-1",
            };
        }

        [[nodiscard]] auto MakeMultipartAbortJob() -> StorageJob {
            const disk::storage::MultipartUploadDescriptor descriptor{
                .key = "staging/upload-123/assembled/7.bin",
                .upload_id = "remote-upload-id",
            };
            Json::Value payload(Json::objectValue);
            payload["backend"] = "s3";
            payload["key"] = descriptor.key;
            payload["upload_id"] = descriptor.upload_id;
            return StorageJob{
                .id = 44,
                .job_type = std::string(kMultipartAbortJobType),
                .aggregate_id = disk::storage::BuildMultipartUploadRecoveryId(descriptor),
                .dedupe_key = disk::storage::BuildMultipartUploadRecoveryDedupeKey(descriptor),
                .payload = std::move(payload),
                .status = StorageJobStatus::Running,
                .attempts = 1,
                .max_attempts = 8,
                .locked_by = "worker-1",
            };
        }

        [[nodiscard]] auto MakePersistedJob(NewStorageJob job) -> StorageJob {
            return StorageJob{
                .id = 45,
                .job_type = std::move(job.job_type),
                .aggregate_id = std::move(job.aggregate_id),
                .dedupe_key = std::move(job.dedupe_key),
                .payload = std::move(job.payload),
                .status = StorageJobStatus::Running,
                .attempts = 1,
                .max_attempts = job.max_attempts,
                .locked_by = "worker-1",
            };
        }

        TEST(StorageJobWorkerHandlerTest, ExecutesPersistedStagingSessionExactly) {
            RecordingStagingStorage storage;
            StorageJobWorker worker(nullptr, &storage, nullptr, "worker-1");

            auto result = drogon::sync_wait(worker.ExecuteJob(MakeCleanupJob()));

            EXPECT_TRUE(result.succeeded);
            ASSERT_EQ(storage.cleaned_sessions.size(), 1U);
            EXPECT_EQ(storage.cleaned_sessions[0].upload_id, "upload-123");
            EXPECT_EQ(storage.cleaned_sessions[0].backend, disk::storage::UploadStagingBackend::S3);
            EXPECT_EQ(storage.cleaned_sessions[0].prefix, "staging/upload-123");
        }

        TEST(StorageJobWorkerHandlerTest, RejectsMismatchedAggregateWithoutDeleting) {
            RecordingStagingStorage storage;
            StorageJobWorker worker(nullptr, &storage, nullptr, "worker-1");
            auto job = MakeCleanupJob();
            job.aggregate_id = "different-upload";

            auto result = drogon::sync_wait(worker.ExecuteJob(job));

            EXPECT_FALSE(result.succeeded);
            EXPECT_FALSE(result.retryable);
            EXPECT_TRUE(storage.cleaned_sessions.empty());
        }

        TEST(StorageJobWorkerHandlerTest, ClassifiesStorageErrors) {
            RecordingStagingStorage storage;
            StorageJobWorker worker(nullptr, &storage, nullptr, "worker-1");
            storage.cleanup_error = ErrorInfo(ErrorCode::InternalError, "temporary outage");

            auto retryable = drogon::sync_wait(worker.ExecuteJob(MakeCleanupJob()));
            EXPECT_FALSE(retryable.succeeded);
            EXPECT_TRUE(retryable.retryable);

            storage.cleanup_error = ErrorInfo(ErrorCode::ValidationFailed, "unsafe prefix");
            auto permanent = drogon::sync_wait(worker.ExecuteJob(MakeCleanupJob()));
            EXPECT_FALSE(permanent.succeeded);
            EXPECT_FALSE(permanent.retryable);
        }

        TEST(StorageJobWorkerHandlerTest, UnknownTypesArePermanentFailures) {
            RecordingStagingStorage storage;
            StorageJobWorker worker(nullptr, &storage, nullptr, "worker-1");
            auto job = MakeCleanupJob();
            job.job_type = "future_job";

            auto result = drogon::sync_wait(worker.ExecuteJob(job));

            EXPECT_FALSE(result.succeeded);
            EXPECT_FALSE(result.retryable);
            EXPECT_TRUE(storage.cleaned_sessions.empty());
        }

        TEST(StorageJobWorkerHandlerTest, RejectsMalformedBlobGcBeforeStorageAccess) {
            StorageJobWorker worker(nullptr, nullptr, nullptr, "worker-1");
            auto job = MakeBlobGcJob();
            job.aggregate_id = "92";

            auto result = drogon::sync_wait(worker.ExecuteJob(job));

            EXPECT_FALSE(result.succeeded);
            EXPECT_FALSE(result.retryable);
        }

        TEST(StorageJobWorkerHandlerTest, RetriesValidBlobGcWhenDependencyIsUnavailable) {
            StorageJobWorker worker(nullptr, nullptr, nullptr, "worker-1");

            auto result = drogon::sync_wait(worker.ExecuteJob(MakeBlobGcJob()));

            EXPECT_FALSE(result.succeeded);
            EXPECT_TRUE(result.retryable);
            EXPECT_EQ(result.error, "Blob GC database is not configured");
        }

        TEST(StorageJobWorkerHandlerTest, AbortsValidatedMultipartUpload) {
            RecordingMultipartUploadCleaner cleaner;
            StorageJobWorker worker(nullptr, nullptr, nullptr, "worker-1", {}, &cleaner);

            auto result = drogon::sync_wait(worker.ExecuteJob(MakeMultipartAbortJob()));

            EXPECT_TRUE(result.succeeded);
            ASSERT_EQ(cleaner.aborted.size(), 1U);
            EXPECT_EQ(cleaner.aborted[0].key, "staging/upload-123/assembled/7.bin");
            EXPECT_EQ(cleaner.aborted[0].upload_id, "remote-upload-id");
        }

        TEST(StorageJobWorkerHandlerTest, RejectsMultipartDigestMismatchBeforeStorageAccess) {
            RecordingMultipartUploadCleaner cleaner;
            StorageJobWorker worker(nullptr, nullptr, nullptr, "worker-1", {}, &cleaner);
            auto job = MakeMultipartAbortJob();
            job.aggregate_id = std::string(64, '0');

            auto result = drogon::sync_wait(worker.ExecuteJob(job));

            EXPECT_FALSE(result.succeeded);
            EXPECT_FALSE(result.retryable);
            EXPECT_TRUE(cleaner.aborted.empty());
        }

        TEST(StorageJobWorkerHandlerTest, ClassifiesMultipartAbortErrors) {
            RecordingMultipartUploadCleaner cleaner;
            StorageJobWorker worker(nullptr, nullptr, nullptr, "worker-1", {}, &cleaner);

            cleaner.error = ErrorInfo(ErrorCode::InternalError, "temporary outage");
            auto retryable = drogon::sync_wait(worker.ExecuteJob(MakeMultipartAbortJob()));
            EXPECT_FALSE(retryable.succeeded);
            EXPECT_TRUE(retryable.retryable);

            cleaner.error = ErrorInfo(ErrorCode::ValidationFailed, "unsafe key");
            auto permanent = drogon::sync_wait(worker.ExecuteJob(MakeMultipartAbortJob()));
            EXPECT_FALSE(permanent.succeeded);
            EXPECT_FALSE(permanent.retryable);
        }

        TEST(StorageJobWorkerHandlerTest, DispatchesExpireUploadsThroughRegistry) {
            StorageJobWorker worker(nullptr, nullptr, nullptr, "worker-1");
            auto built = BuildExpireUploadsJob(ExpireUploadsPageRequest{
                .scan_id = "scan-expire",
                .page = 0,
                .limit = 100,
            });
            ASSERT_TRUE(built.has_value());

            auto result = drogon::sync_wait(
                worker.ExecuteJob(MakePersistedJob(std::move(built.value())))
            );

            EXPECT_FALSE(result.succeeded);
            EXPECT_TRUE(result.retryable);
            EXPECT_EQ(result.error, "Upload expiration database is not configured");
        }

        TEST(StorageJobWorkerHandlerTest, DispatchesExpireTrashThroughRegistry) {
            StorageJobWorker worker(nullptr, nullptr, nullptr, "worker-1");
            auto built = BuildExpireTrashJob(ExpireTrashPageRequest{
                .scan_id = "scan-trash",
                .limit = 100,
            });
            ASSERT_TRUE(built.has_value());

            auto result = drogon::sync_wait(
                worker.ExecuteJob(MakePersistedJob(std::move(built.value())))
            );

            EXPECT_FALSE(result.succeeded);
            EXPECT_TRUE(result.retryable);
            EXPECT_EQ(result.error, "Trash expiration database is not configured");
        }

        TEST(StorageJobWorkerHandlerTest, DispatchesReconciliationThroughRegistry) {
            StorageJobWorker worker(nullptr, nullptr, nullptr, "worker-1");
            auto built = BuildStorageReconcileJob(
                disk::reconciliation::ReconciliationPageRequest{
                    .scan_id = "scan-reconcile",
                    .scope = disk::reconciliation::ReconciliationScope::Users,
                    .limit = 100,
                }
            );
            ASSERT_TRUE(built.has_value());

            auto result = drogon::sync_wait(
                worker.ExecuteJob(MakePersistedJob(std::move(built.value())))
            );

            EXPECT_FALSE(result.succeeded);
            EXPECT_TRUE(result.retryable);
            EXPECT_EQ(result.error, "Storage reconciliation database is not configured");
        }

        TEST(StorageJobWorkerHandlerTest, LogsFailedScanLifecycleWithoutOpaqueCursor) {
            const disk::reconciliation::ReconciliationPageRequest request{
                .scan_id = "scan-log",
                .scope = disk::reconciliation::ReconciliationScope::Staging,
                .continuation_token = "opaque-sensitive-cursor",
                .limit = 100,
            };
            const auto cursor_digest = BuildReconciliationCursorDigest(request);
            auto built = BuildStorageReconcileJob(request);
            ASSERT_TRUE(built.has_value());

            ScopedLogCapture capture;
            StorageJobWorker worker(nullptr, nullptr, nullptr, "worker-log");
            auto result = drogon::sync_wait(
                worker.ExecuteJob(MakePersistedJob(std::move(built.value())))
            );
            const auto logs = capture.Text();

            EXPECT_FALSE(result.succeeded);
            EXPECT_NE(logs.find("Periodic scan page started:"), std::string::npos);
            EXPECT_NE(logs.find("Periodic scan page finished:"), std::string::npos);
            EXPECT_NE(logs.find("instance_id=worker-log"), std::string::npos);
            EXPECT_NE(logs.find("job_type=storage_reconcile"), std::string::npos);
            EXPECT_NE(logs.find("scan_id=scan-log"), std::string::npos);
            EXPECT_NE(logs.find("scope=staging"), std::string::npos);
            EXPECT_NE(logs.find("cursor_kind=continuation_digest"), std::string::npos);
            EXPECT_NE(logs.find("current_cursor=" + cursor_digest), std::string::npos);
            EXPECT_NE(logs.find("outcome=failure"), std::string::npos);
            EXPECT_NE(logs.find("duration_ms="), std::string::npos);
            EXPECT_NE(logs.find("candidates=0"), std::string::npos);
            EXPECT_NE(logs.find("succeeded=0"), std::string::npos);
            EXPECT_NE(logs.find("failed=1"), std::string::npos);
            EXPECT_NE(logs.find("next_cursor=" + cursor_digest), std::string::npos);
            EXPECT_EQ(logs.find(request.continuation_token), std::string::npos);
        }

        TEST(StorageJobWorkerHandlerTest, RejectsTamperedPeriodicTaskBeforeDatabaseAccess) {
            StorageJobWorker worker(nullptr, nullptr, nullptr, "worker-1");
            auto built = BuildExpireUploadsJob(ExpireUploadsPageRequest{
                .scan_id = "scan-expire",
                .page = 0,
                .limit = 100,
            });
            ASSERT_TRUE(built.has_value());
            auto job = MakePersistedJob(std::move(built.value()));
            job.aggregate_id = "different-scan";

            auto result = drogon::sync_wait(worker.ExecuteJob(job));

            EXPECT_FALSE(result.succeeded);
            EXPECT_FALSE(result.retryable);
            EXPECT_EQ(result.error, "expire_uploads aggregate_id does not match scan_id");
        }

        TEST(StorageJobWorkerRetryTest, AppliesBoundedStableJitter) {
            const auto first = StorageJobWorker::ComputeRetryDelaySeconds(42, 1, 5, 3600);
            const auto second = StorageJobWorker::ComputeRetryDelaySeconds(42, 2, 5, 3600);
            const auto repeated = StorageJobWorker::ComputeRetryDelaySeconds(42, 2, 5, 3600);
            const auto capped = StorageJobWorker::ComputeRetryDelaySeconds(42, 40, 5, 3600);

            EXPECT_GE(first, 5U);
            EXPECT_LE(first, 6U);
            EXPECT_GE(second, 10U);
            EXPECT_LE(second, 12U);
            EXPECT_EQ(second, repeated);
            EXPECT_EQ(capped, 3600U);
        }
    } // namespace
} // namespace disk::jobs
