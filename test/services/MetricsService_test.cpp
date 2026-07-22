#include "services/MetricsService.hpp"

#include <chrono>
#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <trantor/utils/ConcurrentTaskQueue.h>

#include "services/StorageJobRepository.hpp"
#include "services/StorageReconciliationService.hpp"

namespace disk::metrics {
    namespace {
        TEST(MetricsServiceTest, ClassifiesOnlyBoundedOperations) {
            EXPECT_EQ(ClassifyHttpOperation("/api/health/ready"), HttpOperation::Health);
            EXPECT_EQ(ClassifyHttpOperation("/metrics"), HttpOperation::Metrics);
            EXPECT_EQ(ClassifyHttpOperation("/api/auth/login"), HttpOperation::Auth);
            EXPECT_EQ(ClassifyHttpOperation("/api/file/list"), HttpOperation::FileQuery);
            EXPECT_EQ(ClassifyHttpOperation("/api/file/123"), HttpOperation::FileQuery);
            EXPECT_EQ(ClassifyHttpOperation("/api/file/search"), HttpOperation::FileQuery);
            EXPECT_EQ(HttpOperationName(HttpOperation::FileQuery), "file_query");
            EXPECT_EQ(ClassifyHttpOperation("/api/file/upload/init"), HttpOperation::UploadInit);
            EXPECT_EQ(ClassifyHttpOperation("/api/file/upload/chunk"), HttpOperation::UploadChunk);
            EXPECT_EQ(ClassifyHttpOperation("/api/file/upload/complete"), HttpOperation::UploadComplete);
            EXPECT_EQ(ClassifyHttpOperation("/api/file/upload/upload-123"), HttpOperation::UploadCancel);
            EXPECT_EQ(ClassifyHttpOperation("/api/file/download/77"), HttpOperation::Download);
            EXPECT_EQ(ClassifyHttpOperation("/api/file/not-a-number"), HttpOperation::Other);
            EXPECT_EQ(ClassifyHttpOperation("/api/file/123/rename"), HttpOperation::Other);
            EXPECT_EQ(ClassifyHttpOperation("/api/file/move"), HttpOperation::Other);
            EXPECT_EQ(ClassifyHttpOperation("/api/file/copy"), HttpOperation::Other);
            EXPECT_EQ(ClassifyHttpOperation("/api/file/delete"), HttpOperation::Other);
            EXPECT_EQ(ClassifyHttpOperation("/api/file"), HttpOperation::Other);
            EXPECT_EQ(ClassifyHttpOperation("/api/share/secret-id"), HttpOperation::Share);
            EXPECT_EQ(
                ClassifyHttpOperation("/api/admin/maintenance/cleanup/expired"),
                HttpOperation::Cleanup
            );
            EXPECT_EQ(HttpOperationName(HttpOperation::Cleanup), "cleanup");
            EXPECT_EQ(ClassifyHttpOperation("/api/admin/users"), HttpOperation::Admin);
            EXPECT_EQ(
                ClassifyHttpOperation("/api/admin/maintenance/cleanup/expired/extra"),
                HttpOperation::Admin
            );
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

        TEST(MetricsServiceTest, DependencyAndQueueNamesUseOnlyFixedLabels) {
            EXPECT_EQ(DependencyName(Dependency::PostgreSql), "postgresql");
            EXPECT_EQ(DependencyName(Dependency::Redis), "redis");
            EXPECT_EQ(DependencyName(Dependency::S3), "s3");
            EXPECT_EQ(DependencyName(Dependency::Count), "unknown");

            EXPECT_EQ(DependencyOutcomeName(DependencyOutcome::Success), "success");
            EXPECT_EQ(DependencyOutcomeName(DependencyOutcome::Timeout), "timeout");
            EXPECT_EQ(DependencyOutcomeName(DependencyOutcome::Connection), "connection");
            EXPECT_EQ(DependencyOutcomeName(DependencyOutcome::Conflict), "conflict");
            EXPECT_EQ(DependencyOutcomeName(DependencyOutcome::NotFound), "not_found");
            EXPECT_EQ(DependencyOutcomeName(DependencyOutcome::Retryable), "retryable");
            EXPECT_EQ(DependencyOutcomeName(DependencyOutcome::Permanent), "permanent");
            EXPECT_EQ(DependencyOutcomeName(DependencyOutcome::Protocol), "protocol");
            EXPECT_EQ(DependencyOutcomeName(DependencyOutcome::Count), "other");

            EXPECT_EQ(ThreadQueueName(ThreadQueue::LocalFile), "local_file");
            EXPECT_EQ(ThreadQueueName(ThreadQueue::LocalAssembly), "local_assembly");
            EXPECT_EQ(ThreadQueueName(ThreadQueue::LocalBlob), "local_blob");
            EXPECT_EQ(ThreadQueueName(ThreadQueue::S3), "s3");
            EXPECT_EQ(ThreadQueueName(ThreadQueue::Count), "unknown");
        }

        TEST(MetricsServiceTest, RegistryTracksDependencyPressureAndRegisteredQueue) {
            auto& registry = MetricsRegistry::GetInstance();
            const auto dependency = static_cast<size_t>(Dependency::Redis);
            const auto timeout = static_cast<size_t>(DependencyOutcome::Timeout);
            const auto before = registry.Snapshot();

            registry.BeginDependencyCall(Dependency::Redis);
            const auto during = registry.Snapshot();
            EXPECT_EQ(
                during.dependency_calls_inflight[dependency],
                before.dependency_calls_inflight[dependency] + 1
            );
            EXPECT_EQ(
                during.dependency_pool_demand[dependency],
                before.dependency_pool_demand[dependency] + 1
            );

            registry.RecordDependencyCall(
                Dependency::Redis,
                DependencyOutcome::Timeout,
                std::chrono::milliseconds(25)
            );
            const auto after = registry.Snapshot();
            EXPECT_EQ(
                after.dependency_calls[dependency][timeout],
                before.dependency_calls[dependency][timeout] + 1
            );
            EXPECT_EQ(
                after.dependency_duration_count[dependency],
                before.dependency_duration_count[dependency] + 1
            );
            EXPECT_EQ(
                after.dependency_calls_inflight[dependency],
                before.dependency_calls_inflight[dependency]
            );
            EXPECT_EQ(
                after.dependency_pool_demand[dependency],
                before.dependency_pool_demand[dependency]
            );

            auto queue = std::make_shared<trantor::ConcurrentTaskQueue>(1, "metrics-test");
            registry.RegisterThreadQueue(ThreadQueue::LocalFile, queue, 1);
            const auto registered = registry.Snapshot();
            const auto queue_index = static_cast<size_t>(ThreadQueue::LocalFile);
            EXPECT_EQ(registered.thread_queue_depth[queue_index], 0);
            EXPECT_EQ(registered.thread_queue_workers[queue_index], 1);
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
            const auto postgresql = static_cast<size_t>(Dependency::PostgreSql);
            metrics.dependency_calls[postgresql]
                                    [static_cast<size_t>(DependencyOutcome::Success)] = 5;
            metrics.dependency_duration_count[postgresql] = 5;
            metrics.dependency_duration_microseconds[postgresql] = 250'000;
            metrics.dependency_calls_inflight[postgresql] = 2;
            metrics.dependency_pool_capacity[postgresql] = 8;
            metrics.dependency_pool_demand[postgresql] = 3;
            metrics.dependency_pool_leases[postgresql] = 1;
            const auto s3_queue = static_cast<size_t>(ThreadQueue::S3);
            metrics.thread_queue_depth[s3_queue] = 6;
            metrics.thread_queue_workers[s3_queue] = 4;
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
            EXPECT_NE(output.find("disk_dependency_calls_total{dependency=\"postgresql\",outcome=\"success\"} 5"), std::string::npos);
            EXPECT_NE(output.find("disk_dependency_call_duration_seconds_count{dependency=\"postgresql\"} 5"), std::string::npos);
            EXPECT_NE(output.find("disk_dependency_calls_inflight{dependency=\"postgresql\"} 2"), std::string::npos);
            EXPECT_NE(output.find("disk_dependency_pool_capacity{dependency=\"postgresql\"} 8"), std::string::npos);
            EXPECT_NE(output.find("disk_dependency_pool_demand{dependency=\"postgresql\"} 4"), std::string::npos);
            EXPECT_NE(output.find("disk_dependency_pool_utilization_ratio{dependency=\"postgresql\"} 0.5"), std::string::npos);
            EXPECT_NE(output.find("disk_thread_queue_depth{queue=\"s3\"} 6"), std::string::npos);
            EXPECT_NE(output.find("disk_thread_queue_workers{queue=\"s3\"} 4"), std::string::npos);
            EXPECT_NE(output.find("disk_storage_jobs{status=\"dead_letter\"} 2"), std::string::npos);
            EXPECT_NE(output.find("disk_upload_tasks{status=\"finalizing\"} 5"), std::string::npos);
            EXPECT_NE(output.find("disk_upload_tasks_active 12"), std::string::npos);
            EXPECT_NE(output.find("disk_reconciliation_findings_unresolved{finding_type=\"missing_final_blob\"} 4"), std::string::npos);
            EXPECT_NE(output.find("disk_reconciliation_findings_unresolved{finding_type=\"upload_staging_mismatch\"} 2"), std::string::npos);
            EXPECT_NE(output.find("disk_metrics_snapshot_success 1"), std::string::npos);
            EXPECT_NE(output.find("disk_worker_claiming_enabled 0"), std::string::npos);
            EXPECT_NE(output.find("disk_worker_accepting_jobs 0"), std::string::npos);
            EXPECT_NE(output.find("instance_id=\"api-quoted\\\"\""), std::string::npos);
            EXPECT_EQ(output.find("upload_id="), std::string::npos);
            EXPECT_EQ(output.find("job_id="), std::string::npos);
        }

        TEST(MetricsServiceTest, RendersClaimingAndAcceptanceAsIndependentWorkerGauges) {
            MetricsSnapshot metrics;
            DatabaseMetricsSnapshot database;
            disk::runtime::ProcessRuntimeState runtime(
                disk::utils::ProcessRole::Worker,
                "worker-1"
            );
            runtime.MarkInitialized();
            runtime.SetWorkerAccepting(true);

            auto output = MetricsService::RenderSnapshot(metrics, database, runtime);
            EXPECT_NE(output.find("disk_worker_claiming_enabled 1"), std::string::npos);
            EXPECT_NE(output.find("disk_worker_accepting_jobs 1"), std::string::npos);

            static_cast<void>(runtime.BeginDrain());
            output = MetricsService::RenderSnapshot(metrics, database, runtime);
            EXPECT_NE(output.find("disk_worker_claiming_enabled 1"), std::string::npos);
            EXPECT_NE(output.find("disk_worker_accepting_jobs 0"), std::string::npos);
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
