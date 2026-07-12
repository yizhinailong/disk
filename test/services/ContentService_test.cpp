#include "services/ContentService.hpp"

#include <gtest/gtest.h>

#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace disk::content {
    namespace {

        TEST(ContentServiceCompileTest, CanConstructWithNullDbClient) {
            ContentService service(nullptr);
            SUCCEED();
        }

        TEST(ContentServiceContractTest, NewContentDefaultsToSingleReference) {
            NewContent content;
            content.hash_md5 = "0123456789abcdef0123456789abcdef";
            content.hash_sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
            content.size = 42;
            content.storage_path = "build/uploaded/01/blob.bin";

            EXPECT_EQ(content.ref_count, 1);
            EXPECT_EQ(content.size, 42U);
            EXPECT_EQ(content.hash_md5.size(), 32U);
            EXPECT_EQ(content.hash_sha256.size(), 64U);
        }

        TEST(ContentServiceContractTest, ZeroRefContentCarriesVerifiedDeletionCandidate) {
            ZeroRefContent candidate{ .id = 7, .storage_path = "build/uploaded/aa/blob.bin" };

            EXPECT_EQ(candidate.id, 7U);
            EXPECT_EQ(candidate.storage_path, "build/uploaded/aa/blob.bin");
        }

        TEST(ContentServiceContractTest, OwnsFileContentsLifecycleBoundary) {
            /// file_contents lookup, creation, ref-count mutation, and zero-ref verification are
            /// intentionally exposed together so callers keep DB changes transaction-aware.
            using FindDefaultResult = decltype(std::declval<ContentService&>().FindByMd5(
                std::declval<const std::string&>()
            ));
            using FindWithClientResult = decltype(std::declval<ContentService&>().FindByMd5(
                std::declval<const drogon::orm::DbClientPtr&>(),
                std::declval<const std::string&>()
            ));
            using FindExistingIdsResult = decltype(std::declval<ContentService&>().FindExistingIds(
                std::declval<const drogon::orm::DbClientPtr&>(),
                std::declval<const std::vector<uint64_t>&>()
            ));
            using CreateResult = decltype(std::declval<ContentService&>().Create(
                std::declval<const drogon::orm::DbClientPtr&>(),
                std::declval<const NewContent&>()
            ));
            using IncrementResult = decltype(std::declval<ContentService&>().IncrementRefCount(
                std::declval<const drogon::orm::DbClientPtr&>(),
                uint64_t{ 1 },
                uint64_t{ 1 }
            ));
            using BatchIncrementResult = decltype(std::declval<ContentService&>().IncrementRefCounts(
                std::declval<const drogon::orm::DbClientPtr&>(),
                std::declval<const std::unordered_map<uint64_t, uint64_t>&>(),
                std::declval<const std::unordered_set<uint64_t>&>()
            ));
            using CheckedBatchIncrementResult = decltype(std::declval<ContentService&>().IncrementRefCountsChecked(
                std::declval<const drogon::orm::DbClientPtr&>(),
                std::declval<const std::unordered_map<uint64_t, uint64_t>&>(),
                std::declval<const std::unordered_set<uint64_t>&>()
            ));
            using DecrementResult = decltype(std::declval<ContentService&>().DecrementRefCounts(
                std::declval<const drogon::orm::DbClientPtr&>(),
                std::declval<const std::unordered_map<uint64_t, uint64_t>&>()
            ));
            using VerifyResult = decltype(std::declval<ContentService&>().VerifyZeroRefContents(
                std::declval<const drogon::orm::DbClientPtr&>(),
                std::declval<const std::vector<uint64_t>&>()
            ));

            EXPECT_TRUE((std::is_same_v<FindDefaultResult, drogon::Task<std::optional<ContentMetadata>>>));
            EXPECT_TRUE((std::is_same_v<FindWithClientResult, drogon::Task<std::optional<ContentMetadata>>>));
            EXPECT_TRUE((std::is_same_v<FindExistingIdsResult, drogon::Task<std::unordered_set<uint64_t>>>));
            EXPECT_TRUE((std::is_same_v<CreateResult, drogon::Task<ContentMetadata>>));
            EXPECT_TRUE((std::is_same_v<IncrementResult, drogon::Task<Result<void>>>));
            EXPECT_TRUE((std::is_same_v<BatchIncrementResult, drogon::Task<std::unordered_set<uint64_t>>>));
            EXPECT_TRUE((std::is_same_v<CheckedBatchIncrementResult, drogon::Task<Result<std::unordered_set<uint64_t>>>>));
            EXPECT_TRUE((std::is_same_v<DecrementResult, drogon::Task<std::vector<ZeroRefContent>>>));
            EXPECT_TRUE((std::is_same_v<VerifyResult, drogon::Task<std::vector<ZeroRefContent>>>));
        }

    } ///< namespace
} ///< namespace disk::content
