#include "services/MetricsService.hpp"

#include <chrono>
#include <string>

#include <gtest/gtest.h>

#include "services/StorageJobRepository.hpp"
#include "services/StorageReconciliationService.hpp"

namespace disk::metrics {
    namespace {
        TEST(MetricsServiceTest, ClassifiesOnlyBoundedOperations) {
            EXPECT_EQ(ClassifyHttpOperation("/api/health/ready"), HttpOperation::Health);
            EXPECT_EQ(ClassifyHttpOperation("/metrics"), HttpOperation::Metrics);
            EXPECT_EQ(ClassifyHttpOperation("/api/auth/login"), HttpOperation::Auth);
            EXPECT_EQ(ClassifyHttpOperation("/api/file/upload/init"), HttpOperation::UploadInit);
            EXPECT_EQ(ClassifyHttpOperation("/api/file/upload/chunk"), HttpOperation::UploadChunk);
            EXPECT_EQ(ClassifyHttpOperation("/api/file/upload/complete"), HttpOperation::UploadComplete);
            EXPECT_EQ(ClassifyHttpOperation("/api/file/upload/upload-123"), HttpOperation::UploadCancel);
            EXPECT_EQ(ClassifyHttpOperation("/api/file/download/77"), HttpOperation::Download);
            EXPECT_EQ(ClassifyHttpOperation("/api/share/secret-id"), HttpOperation::Share);
            EXPECT_EQ(ClassifyHttpOperation("/api/admin/users"), HttpOperation::Admin);
            EXPECT_EQ(ClassifyHttpOperation("/api/folder/list"), HttpOperation::Other);
        }

        TEST(MetricsServiceTest, RegistryRecordsCumulativeBucketsAndFixedJobTypes) {
            auto& registry = MetricsRegistry::GetInstance();
            const auto before = registry.Snapshot();

            registry.RecordHttpRequest(
                HttpOperation::UploadChunk,
                201,
                std::chrono::microseconds(7'500)
            );
            registry.RecordStorageJob(
                disk::jobs::kBlobGcJobType,
                StorageJobOutcome::Retry,
                std::chrono::microseconds(30'000),
                true
            );
            const auto after = registry.Snapshot();

            const auto upload_chunk = static_cast<size_t>(HttpOperation::UploadChunk);
            const auto success = static_cast<size_t>(HttpStatusClass::Success);
            EXPECT_EQ(
                after.http_requests[upload_chunk][success],
                before.http_requests[upload_chunk][success] + 1
            );
            EXPECT_EQ(
                after.http_duration_buckets[upload_chunk][0],
                before.http_duration_buckets[upload_chunk][0]
            );
            EXPECT_EQ(
                after.http_duration_buckets[upload_chunk][1],
                before.http_duration_buckets[upload_chunk][1] + 1
            );

            constexpr size_t blob_gc_index = 2;
            const auto retry = static_cast<size_t>(StorageJobOutcome::Retry);
            EXPECT_EQ(
                after.storage_job_runs[blob_gc_index][retry],
                before.storage_job_runs[blob_gc_index][retry] + 1
            );
            EXPECT_EQ(
                after.storage_job_takeovers[blob_gc_index],
                before.storage_job_takeovers[blob_gc_index] + 1
            );
        }

        TEST(MetricsServiceTest, RendersProcessAndDatabaseSnapshotWithoutHighCardinalityLabels) {
            MetricsSnapshot metrics;
            metrics.http_requests[static_cast<size_t>(HttpOperation::Admin)]
                                 [static_cast<size_t>(HttpStatusClass::ClientError)] = 3;
            DatabaseMetricsSnapshot database;
            database.storage_jobs[4] = 2;
            database.upload_tasks[4] = 5;
            database.oldest_ready_job_age_seconds = 12.5;
            database.expired_job_leases = 1;
            database.reconciliation_findings[ReconciliationFindingTypeIndex(
                "missing_final_blob"
            )] = 4;
            database.reconciliation_findings[ReconciliationFindingTypeIndex(
                disk::reconciliation::kUploadStagingMismatchFindingType
            )] = 2;
            database.success = true;
            disk::runtime::ProcessRuntimeState runtime(
                disk::utils::ProcessRole::Api,
                "api-quoted\""
            );
            runtime.MarkInitialized();

            const auto output = MetricsService::RenderSnapshot(metrics, database, runtime);

            EXPECT_NE(output.find("disk_http_requests_total{operation=\"admin\",status_class=\"4xx\"} 3"), std::string::npos);
            EXPECT_NE(output.find("disk_storage_jobs{status=\"dead_letter\"} 2"), std::string::npos);
            EXPECT_NE(output.find("disk_upload_tasks{status=\"finalizing\"} 5"), std::string::npos);
            EXPECT_NE(output.find("disk_reconciliation_findings_unresolved{finding_type=\"missing_final_blob\"} 4"), std::string::npos);
            EXPECT_NE(output.find("disk_reconciliation_findings_unresolved{finding_type=\"upload_staging_mismatch\"} 2"), std::string::npos);
            EXPECT_NE(output.find("disk_metrics_snapshot_success 1"), std::string::npos);
            EXPECT_NE(output.find("instance_id=\"api-quoted\\\"\""), std::string::npos);
            EXPECT_EQ(output.find("upload_id="), std::string::npos);
            EXPECT_EQ(output.find("job_id="), std::string::npos);
        }

        TEST(MetricsServiceTest, ReconciliationFindingTypesUseFixedUnknownBucket) {
            EXPECT_EQ(ReconciliationFindingTypeIndex("content_ref_count_mismatch"), 0);
            EXPECT_NE(
                ReconciliationFindingTypeIndex(
                    disk::reconciliation::kUploadStagingMismatchFindingType
                ),
                kReconciliationFindingTypeCount - 1
            );
            EXPECT_EQ(
                ReconciliationFindingTypeIndex("future_finding_type"),
                kReconciliationFindingTypeCount - 1
            );
        }
    } // namespace
} // namespace disk::metrics
