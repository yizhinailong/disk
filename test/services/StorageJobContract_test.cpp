#include "services/StorageJobContract.hpp"

#include <limits>
#include <string>
#include <utility>

#include <gtest/gtest.h>

namespace disk::jobs {
    namespace {
        [[nodiscard]] auto ToPersistedJob(NewStorageJob job) -> StorageJob {
            return StorageJob{
                .id = 1,
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

        TEST(StorageJobContractTest, RoundTripsExpireUploadsPage) {
            const ExpireUploadsPageRequest request{
                .scan_id = "2026-07-19T12:00Z",
                .page = 7,
                .limit = kMaxExpireUploadsPageSize,
            };

            auto built = BuildExpireUploadsJob(request);
            ASSERT_TRUE(built.has_value());
            EXPECT_EQ(built->aggregate_id, request.scan_id);
            EXPECT_EQ(
                built->dedupe_key,
                "periodic:expire-uploads:2026-07-19T12:00Z:7"
            );

            auto parsed = ParseExpireUploadsJob(ToPersistedJob(std::move(built.value())));
            ASSERT_TRUE(parsed.has_value());
            EXPECT_EQ(parsed->scan_id, request.scan_id);
            EXPECT_EQ(parsed->page, request.page);
            EXPECT_EQ(parsed->limit, request.limit);
        }

        TEST(StorageJobContractTest, RejectsExpireUploadsIdentityTampering) {
            auto built = BuildExpireUploadsJob(ExpireUploadsPageRequest{
                .scan_id = "scan-1",
                .page = 0,
                .limit = 100,
            });
            ASSERT_TRUE(built.has_value());
            auto job = ToPersistedJob(std::move(built.value()));
            job.dedupe_key = "periodic:expire-uploads:scan-1:1";

            auto parsed = ParseExpireUploadsJob(job);
            ASSERT_FALSE(parsed.has_value());
            EXPECT_EQ(parsed.error(), "expire_uploads dedupe_key does not match payload");
        }

        TEST(StorageJobContractTest, RejectsUnsafeOrUnboundedExpireUploadsPayload) {
            EXPECT_FALSE(BuildExpireUploadsJob(ExpireUploadsPageRequest{
                                                   .scan_id = "../scan",
                                                   .limit = 100,
                                               })
                             .has_value());
            EXPECT_FALSE(BuildExpireUploadsJob(ExpireUploadsPageRequest{
                                                   .scan_id = "scan-1",
                                                   .page = std::numeric_limits<uint64_t>::max(),
                                                   .limit = 100,
                                               })
                             .has_value());

            auto built = BuildExpireUploadsJob(ExpireUploadsPageRequest{
                .scan_id = "scan-1",
                .limit = 100,
            });
            ASSERT_TRUE(built.has_value());
            auto job = ToPersistedJob(std::move(built.value()));
            job.payload["unexpected"] = true;
            auto parsed = ParseExpireUploadsJob(job);
            ASSERT_FALSE(parsed.has_value());
            EXPECT_EQ(parsed.error(), "expire_uploads payload has an invalid object shape");
        }

        TEST(StorageJobContractTest, RoundTripsExpireTrashCursor) {
            const ExpireTrashPageRequest request{
                .scan_id = "20260719T12Z",
                .after_id = 42,
                .limit = kMaxExpireTrashPageSize,
            };

            auto built = BuildExpireTrashJob(request);
            ASSERT_TRUE(built.has_value());
            EXPECT_EQ(built->aggregate_id, request.scan_id);
            EXPECT_EQ(
                built->dedupe_key,
                "periodic:expire-trash:20260719T12Z:42"
            );

            auto parsed = ParseExpireTrashJob(ToPersistedJob(std::move(built.value())));
            ASSERT_TRUE(parsed.has_value());
            EXPECT_EQ(parsed->scan_id, request.scan_id);
            EXPECT_EQ(parsed->after_id, request.after_id);
            EXPECT_EQ(parsed->limit, request.limit);
        }

        TEST(StorageJobContractTest, RejectsTamperedOrUnboundedExpireTrashPayload) {
            EXPECT_FALSE(BuildExpireTrashJob(ExpireTrashPageRequest{
                                                 .scan_id = "../scan",
                                             })
                             .has_value());
            EXPECT_FALSE(BuildExpireTrashJob(ExpireTrashPageRequest{
                                                 .scan_id = "scan-1",
                                                 .after_id = std::numeric_limits<uint64_t>::max(),
                                             })
                             .has_value());
            EXPECT_FALSE(BuildExpireTrashJob(ExpireTrashPageRequest{
                                                 .scan_id = "scan-1",
                                                 .limit = kMaxExpireTrashPageSize + 1,
                                             })
                             .has_value());

            auto built = BuildExpireTrashJob(ExpireTrashPageRequest{
                .scan_id = "scan-1",
                .after_id = 5,
            });
            ASSERT_TRUE(built.has_value());
            auto job = ToPersistedJob(std::move(built.value()));
            job.dedupe_key = "periodic:expire-trash:scan-1:6";
            auto parsed = ParseExpireTrashJob(job);
            ASSERT_FALSE(parsed.has_value());
            EXPECT_EQ(parsed.error(), "expire_trash dedupe_key does not match payload");
        }

        TEST(StorageJobContractTest, UsesDocumentedReconciliationCursorDigest) {
            const disk::reconciliation::ReconciliationPageRequest request{
                .scan_id = "scan-2",
                .scope = disk::reconciliation::ReconciliationScope::Contents,
                .after_id = 0,
                .limit = 500,
            };

            auto built = BuildStorageReconcileJob(request);
            ASSERT_TRUE(built.has_value());
            EXPECT_EQ(
                BuildReconciliationCursorDigest(request),
                "43cb960ae320b159d0f87662c563a9734a2284bcb4ff2016cb2f57c25d0aac82"
            );
            EXPECT_EQ(
                built->dedupe_key,
                "periodic:storage-reconcile:scan-2:contents:" "43cb960ae320b159d0f87662c563a9734a2284bcb4ff2016cb2f57c25d0aac82"
            );

            auto parsed = ParseStorageReconcileJob(ToPersistedJob(std::move(built.value())));
            ASSERT_TRUE(parsed.has_value());
            EXPECT_EQ(parsed->scan_id, request.scan_id);
            EXPECT_EQ(parsed->scope, request.scope);
            EXPECT_EQ(parsed->after_id, 0U);
            EXPECT_TRUE(parsed->continuation_token.empty());
            EXPECT_EQ(parsed->limit, 500U);
        }

        TEST(StorageJobContractTest, RoundTripsOpaqueObjectCursor) {
            const disk::reconciliation::ReconciliationPageRequest request{
                .scan_id = "scan-3",
                .scope = disk::reconciliation::ReconciliationScope::Staging,
                .continuation_token = "opaque+/cursor==",
                .limit = 1000,
            };

            auto built = BuildStorageReconcileJob(request);
            ASSERT_TRUE(built.has_value());
            auto parsed = ParseStorageReconcileJob(ToPersistedJob(std::move(built.value())));
            ASSERT_TRUE(parsed.has_value());
            EXPECT_EQ(parsed->continuation_token, request.continuation_token);
            EXPECT_EQ(parsed->scope, request.scope);
        }

        TEST(StorageJobContractTest, RejectsMixedReconciliationCursors) {
            auto built = BuildStorageReconcileJob(
                disk::reconciliation::ReconciliationPageRequest{
                    .scan_id = "scan-4",
                    .scope = disk::reconciliation::ReconciliationScope::Final,
                    .continuation_token = "opaque",
                    .limit = 100,
                }
            );
            ASSERT_TRUE(built.has_value());
            auto job = ToPersistedJob(std::move(built.value()));
            job.payload["after_id"] = Json::UInt64(1);

            auto parsed = ParseStorageReconcileJob(job);
            ASSERT_FALSE(parsed.has_value());
            EXPECT_EQ(parsed.error(), "Object reconciliation cannot use a database cursor");
        }
    } // namespace
} // namespace disk::jobs
