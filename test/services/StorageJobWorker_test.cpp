#include "services/StorageJobWorker.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "storage/UploadStagingStorage.hpp"

namespace disk::jobs {
    namespace {
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
