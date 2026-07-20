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
            registry.RecordUploadChunk(4'096);
            registry.RecordUploadCompleteStage(
                UploadCompleteStage::Assemble,
                std::chrono::microseconds(30'000)
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
            EXPECT_EQ(after.upload_chunks_total, before.upload_chunks_total + 1);
            EXPECT_EQ(after.upload_chunk_bytes_total, before.upload_chunk_bytes_total + 4'096);

            const auto assemble = static_cast<size_t>(UploadCompleteStage::Assemble);
            EXPECT_EQ(
                after.upload_complete_duration_buckets[assemble][2],
                before.upload_complete_duration_buckets[assemble][2]
            );
            EXPECT_EQ(
                after.upload_complete_duration_buckets[assemble][3],
                before.upload_complete_duration_buckets[assemble][3] + 1
            );
            EXPECT_EQ(
                after.upload_complete_duration_count[assemble],
                before.upload_complete_duration_count[assemble] + 1
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
            metrics.upload_chunks_total = 7;
            metrics.upload_chunk_bytes_total = 12'345;
            const auto promote = static_cast<size_t>(UploadCompleteStage::Promote);
            metrics.upload_complete_duration_buckets[promote][4] = 2;
            metrics.upload_complete_duration_count[promote] = 2;
            metrics.upload_complete_duration_microseconds[promote] = 150'000;
            DatabaseMetricsSnapshot database;
            database.storage_jobs[4] = 2;
            database.upload_tasks[0] = 7;
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
            EXPECT_NE(output.find("disk_upload_chunks_total 7"), std::string::npos);
            EXPECT_NE(output.find("disk_upload_chunk_bytes_total 12345"), std::string::npos);
            EXPECT_NE(output.find("disk_upload_complete_stage_duration_seconds_count{stage=\"promote\"} 2"), std::string::npos);
            EXPECT_NE(output.find("disk_storage_jobs{status=\"dead_letter\"} 2"), std::string::npos);
            EXPECT_NE(output.find("disk_upload_tasks{status=\"finalizing\"} 5"), std::string::npos);
            EXPECT_NE(output.find("disk_upload_tasks_active 12"), std::string::npos);
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
            EXPECT_NE(
                ReconciliationFindingTypeIndex(
                    disk::reconciliation::kFinalBlobReadInterruptedFindingType
                ),
                kReconciliationFindingTypeCount - 1
            );
            EXPECT_EQ(
                ReconciliationFindingTypeIndex("future_finding_type"),
                kReconciliationFindingTypeCount - 1
            );
        }

        TEST(MetricsServiceTest, UploadCompletionStagesUseOnlyFixedNames) {
            EXPECT_EQ(UploadCompleteStageName(UploadCompleteStage::ClaimLease), "claim_lease");
            EXPECT_EQ(UploadCompleteStageName(UploadCompleteStage::LoadMetadata), "load_metadata");
            EXPECT_EQ(UploadCompleteStageName(UploadCompleteStage::Assemble), "assemble");
            EXPECT_EQ(UploadCompleteStageName(UploadCompleteStage::DedupLookup), "dedup_lookup");
            EXPECT_EQ(UploadCompleteStageName(UploadCompleteStage::Promote), "promote");
            EXPECT_EQ(UploadCompleteStageName(UploadCompleteStage::Commit), "commit");
            EXPECT_EQ(UploadCompleteStageName(UploadCompleteStage::Count), "unknown");
        }
    } // namespace
} // namespace disk::metrics
