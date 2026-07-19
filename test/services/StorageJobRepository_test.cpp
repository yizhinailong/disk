#include "services/StorageJobRepository.hpp"

#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace disk::jobs {
    namespace {
        TEST(StorageJobStatusTest, RoundTripsPersistedValues) {
            for (const auto status : {
                     StorageJobStatus::Pending,
                     StorageJobStatus::Running,
                     StorageJobStatus::Retry,
                     StorageJobStatus::Succeeded,
                     StorageJobStatus::DeadLetter,
                 }) {
                EXPECT_EQ(ParseStorageJobStatus(ToStorageValue(status)), status);
            }
            EXPECT_FALSE(ParseStorageJobStatus(5).has_value());
        }

        TEST(StorageJobRepositoryContractTest, ExposesOwnerCheckedQueuePrimitives) {
            using EnqueueResult = decltype(std::declval<const StorageJobRepository&>().Enqueue(
                std::declval<const NewStorageJob&>()
            ));
            using TransactionalEnqueueResult = decltype(std::declval<const StorageJobRepository&>().Enqueue(
                std::declval<const drogon::orm::DbClientPtr&>(),
                std::declval<const NewStorageJob&>()
            ));
            using RearmResult = decltype(std::declval<const StorageJobRepository&>().EnqueueOrRearmSucceeded(
                std::declval<const drogon::orm::DbClientPtr&>(),
                std::declval<const NewStorageJob&>()
            ));
            using BlobGcGateResult = decltype(std::declval<const StorageJobRepository&>().CheckBlobGcReferenceGate(
                std::declval<const drogon::orm::DbClientPtr&>(),
                std::declval<uint64_t>()
            ));
            using ClaimResult = decltype(std::declval<const StorageJobRepository&>().ClaimReadyBatch(
                std::declval<const std::string&>(),
                std::declval<size_t>(),
                std::declval<uint32_t>()
            ));
            using RenewResult = decltype(std::declval<const StorageJobRepository&>().RenewLease(
                std::declval<uint64_t>(),
                std::declval<const std::string&>(),
                std::declval<uint32_t>()
            ));
            using CompleteResult = decltype(std::declval<const StorageJobRepository&>().MarkSucceeded(
                std::declval<uint64_t>(),
                std::declval<const std::string&>()
            ));
            using TransactionalCompleteResult = decltype(std::declval<const StorageJobRepository&>().MarkSucceeded(
                std::declval<const drogon::orm::DbClientPtr&>(),
                std::declval<uint64_t>(),
                std::declval<const std::string&>()
            ));
            using FailureResult = decltype(std::declval<const StorageJobRepository&>().MarkFailed(
                std::declval<uint64_t>(),
                std::declval<const std::string&>(),
                std::declval<const std::string&>(),
                std::declval<bool>(),
                std::declval<uint32_t>()
            ));

            EXPECT_TRUE((std::is_same_v<EnqueueResult, drogon::Task<bool>>));
            EXPECT_TRUE((std::is_same_v<TransactionalEnqueueResult, drogon::Task<bool>>));
            EXPECT_TRUE((std::is_same_v<RearmResult, drogon::Task<bool>>));
            EXPECT_TRUE((std::is_same_v<BlobGcGateResult, drogon::Task<BlobGcReferenceGate>>));
            EXPECT_TRUE((std::is_same_v<ClaimResult, drogon::Task<std::vector<StorageJob>>>));
            EXPECT_TRUE((std::is_same_v<RenewResult, drogon::Task<bool>>));
            EXPECT_TRUE((std::is_same_v<CompleteResult, drogon::Task<bool>>));
            EXPECT_TRUE((std::is_same_v<TransactionalCompleteResult, drogon::Task<bool>>));
            EXPECT_TRUE((std::is_same_v<FailureResult, drogon::Task<std::optional<StorageJobStatus>>>));
        }

        TEST(StorageJobRepositoryContractTest, DefinesRequiredHandlerTypes) {
            EXPECT_EQ(kStagingCleanupJobType, "staging_cleanup");
            EXPECT_EQ(kMultipartAbortJobType, "multipart_abort");
            EXPECT_EQ(kBlobGcJobType, "blob_gc");
            EXPECT_EQ(kExpireUploadsJobType, "expire_uploads");
            EXPECT_EQ(kStorageReconcileJobType, "storage_reconcile");
        }
    } // namespace
} // namespace disk::jobs
