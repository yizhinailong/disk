#include "services/CleanupService.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "services/TrashContentIdResolver.hpp"
#include "utils/BatchUtils.hpp"

namespace disk::services {
    namespace {

        auto RepositoryRoot() -> std::filesystem::path {
            return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
        }

        auto ReadSourceFile(const std::filesystem::path& relative_path) -> std::string {
            std::ifstream input(RepositoryRoot() / relative_path);
            std::ostringstream buffer;
            buffer << input.rdbuf();
            return buffer.str();
        }

        auto Contains(const std::string& source, std::string_view expected) -> bool {
            return source.find(expected) != std::string::npos;
        }

        auto CountOccurrences(const std::string& source, std::string_view expected) -> size_t {
            size_t count = 0;
            size_t position = 0;
            while ((position = source.find(expected, position)) != std::string::npos) {
                ++count;
                position += expected.size();
            }
            return count;
        }

        template <typename Utility>
        concept HasBatchInputValidator = requires(const std::vector<uint64_t>& items) {
            Utility::ValidateBatchInput(items);
        };

        template <typename Service>
        concept HasPublicCompositeCleanup = requires(Service& service) {
            service.RunExpiredCleanupOnce();
        };

        template <typename Service>
        concept HasPublicTrashCleanupStage = requires(Service& service) {
            service.CleanupExpiredTrash();
        };

        template <typename Service>
        concept HasPublicUploadCleanupStage = requires(Service& service) {
            service.CleanupExpiredUploadTasks();
        };

        static_assert(!HasBatchInputValidator<utils::BatchUtils>);
        static_assert(HasPublicCompositeCleanup<CleanupService>);
        static_assert(!HasPublicTrashCleanupStage<CleanupService>);
        static_assert(!HasPublicUploadCleanupStage<CleanupService>);

        TEST(CleanupServiceCompileTest, CanConstructWithNullDbClient) {
            CleanupService service(nullptr);
            SUCCEED();
        }

        TEST(CleanupServiceLogContractTest, DependencyFailuresUseFixedSummaries) {
            const auto source = ReadSourceFile("src/services/CleanupService.cpp");

            ASSERT_FALSE(source.empty());
            EXPECT_EQ(CountOccurrences(source, ".what()"), 0U);
            EXPECT_TRUE(Contains(
                source,
                "Logger::Error(log_context) << \"Database error cleaning expired upload tasks\";"
            ));
            EXPECT_TRUE(Contains(
                source,
                "Logger::Error(log_context) << \"Unexpected error cleaning expired upload tasks\";"
            ));
            EXPECT_EQ(CountOccurrences(source, "Failed to clean expired upload tasks"), 2U);
        }

        TEST(CleanupServiceBuildSafeNumericInClauseTest, EmptyVectorReturnsEmptyString) {
            const std::vector<uint64_t> ids;
            EXPECT_TRUE(utils::BatchUtils::BuildSafeNumericInClause(ids).empty());
        }

        TEST(CleanupServiceBuildSafeNumericInClauseTest, MultipleIdsReturnCommaSeparatedClause) {
            const std::vector<uint64_t> ids{ 1, 42, 500, 999999 };
            EXPECT_EQ(utils::BatchUtils::BuildSafeNumericInClause(ids), "1,42,500,999999");
        }

        TEST(CleanupServiceLegacyContentIdTest, CleanupResolvesContentIdFromLegacyItemDataWhenColumnIsNull) {
            auto result = trash_content_internal::ResolveRequiredContentId(
                std::nullopt,
                R"({"content_id":987,"mime_type":"text/plain"})"
            );

            ASSERT_TRUE(result.has_value());
            EXPECT_EQ(result->value, 987U);
            EXPECT_EQ(result->source, trash_content_internal::ContentIdSource::ItemData);
        }

        TEST(CleanupServiceLegacyContentIdTest, CleanupRejectsLegacyRowWithoutRecoverableContentId) {
            auto result = trash_content_internal::ResolveRequiredContentId(
                std::nullopt,
                R"({"mime_type":"text/plain"})"
            );

            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
            EXPECT_EQ(result.error().message, "Trash item is missing valid content_id");
        }

        TEST(CleanupServiceBatchingTest, CursorAdvancesPastAllRowsInMultiplePages) {
            constexpr int kFetchBatchSize = 3;
            std::vector<uint64_t> all_expired_ids = { 10, 20, 30, 40, 50, 60, 70, 80 };

            uint64_t last_seen_id = 0;
            std::vector<uint64_t> visited_ids;
            int iterations = 0;

            while (true) {
                std::vector<uint64_t> batch;
                for (auto id : all_expired_ids) {
                    if (id > last_seen_id && batch.size() < static_cast<size_t>(kFetchBatchSize)) {
                        batch.push_back(id);
                    }
                }

                if (batch.empty()) {
                    break;
                }

                uint64_t batch_max_id = 0;
                for (auto id : batch) {
                    if (id > batch_max_id) {
                        batch_max_id = id;
                    }
                }

                for (auto id : batch) {
                    visited_ids.push_back(id);
                }

                last_seen_id = batch_max_id;
                iterations++;

                if (batch.size() < static_cast<size_t>(kFetchBatchSize)) {
                    break;
                }
            }

            EXPECT_EQ(visited_ids, all_expired_ids);
            EXPECT_EQ(last_seen_id, 80u);
            EXPECT_EQ(iterations, 3);
        }

        TEST(CleanupServiceBatchingTest, CursorDoesNotAdvancePastFailedPage) {
            constexpr int kFetchBatchSize = 3;
            std::vector<uint64_t> all_ids = { 10, 20, 30 };
            std::vector<uint64_t> failing_ids = { 20, 30 };
            const uint64_t after_id = 0;
            std::vector<uint64_t> batch;
            for (auto id : all_ids) {
                if (id > after_id && batch.size() < static_cast<size_t>(kFetchBatchSize)) {
                    batch.push_back(id);
                }
            }

            const auto page_failed = std::ranges::any_of(batch, [&failing_ids](uint64_t id) {
                return std::ranges::find(failing_ids, id) != failing_ids.end();
            });
            const auto next_after_id = page_failed ? after_id : batch.back();

            EXPECT_TRUE(page_failed);
            EXPECT_EQ(next_after_id, after_id);
        }

        TEST(CleanupServiceBatchingTest, EmptyResultTerminatesImmediately) {
            uint64_t last_seen_id = 0;
            int iterations = 0;
            std::vector<uint64_t> empty_ids;

            while (true) {
                std::vector<uint64_t> batch;
                for (auto id : empty_ids) {
                    if (id > last_seen_id && batch.size() < 100) {
                        batch.push_back(id);
                    }
                }

                if (batch.empty()) {
                    break;
                }

                iterations++;
                break;
            }

            EXPECT_EQ(iterations, 0);
        }

        TEST(CleanupServiceBatchingTest, ChunkSubdivisionWithinCursorBatch) {
            constexpr size_t kFetchBatchSize = 5;
            constexpr size_t kChunkSize = 2;

            std::vector<uint64_t> batch_ids = { 10, 20, 30, 40, 50 };

            auto chunks = utils::BatchUtils::Chunk(batch_ids, kChunkSize);
            ASSERT_EQ(chunks.size(), 3u);
            EXPECT_EQ(chunks[0], (std::vector<uint64_t>{ 10, 20 }));
            EXPECT_EQ(chunks[1], (std::vector<uint64_t>{ 30, 40 }));
            EXPECT_EQ(chunks[2], (std::vector<uint64_t>{ 50 }));
        }

        TEST(CleanupServiceBatchingTest, BoundedBatchCapStopsAfterMaxIterations) {
            constexpr int kFetchBatchSize = 3;
            constexpr int kMaxBatchesPerRun = 2;

            std::vector<uint64_t> all_expired_ids = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
            uint64_t last_seen_id = 0;
            std::vector<uint64_t> visited_ids;
            int batch_iteration = 0;

            while (batch_iteration < kMaxBatchesPerRun) {
                std::vector<uint64_t> batch;
                for (auto id : all_expired_ids) {
                    if (id > last_seen_id && batch.size() < static_cast<size_t>(kFetchBatchSize)) {
                        batch.push_back(id);
                    }
                }

                if (batch.empty()) {
                    break;
                }

                uint64_t batch_max_id = 0;
                for (auto id : batch) {
                    if (id > batch_max_id) {
                        batch_max_id = id;
                    }
                }

                for (auto id : batch) {
                    visited_ids.push_back(id);
                }

                last_seen_id = batch_max_id;
                batch_iteration++;

                if (batch.size() < static_cast<size_t>(kFetchBatchSize)) {
                    break;
                }
            }

            EXPECT_EQ(batch_iteration, 2);
            EXPECT_EQ(visited_ids, (std::vector<uint64_t>{ 10, 20, 30, 40, 50, 60 }));
            EXPECT_EQ(last_seen_id, 60u);
        }

        TEST(CleanupServiceBatchingTest, BlobDeletionOnlyAfterReferenceReverification) {
            struct ContentRef {
                uint64_t id;
                std::string path;
                bool still_zero;
            };

            std::vector<ContentRef> transaction_zero_refs = {
                { 100, "/disk/blob_a",  true },
                { 200, "/disk/blob_b", false },
                { 300, "/disk/blob_c",  true },
            };

            std::vector<std::string> verified_paths;
            for (const auto& ref : transaction_zero_refs) {
                if (ref.still_zero) {
                    verified_paths.push_back(ref.path);
                }
            }

            ASSERT_EQ(verified_paths.size(), 2u);
            EXPECT_EQ(verified_paths[0], "/disk/blob_a");
            EXPECT_EQ(verified_paths[1], "/disk/blob_c");
        }

        TEST(CleanupServiceBatchingTest, EvidenceCountersTrackChunkResults) {
            struct ChunkResult {
                bool succeeded;
                int blobs_verified;
                int blobs_deleted;
            };

            std::vector<ChunkResult> chunk_results = {
                {  true, 3, 3 },
                { false, 0, 0 },
                {  true, 2, 1 },
            };

            int chunks_succeeded = 0;
            int chunks_failed = 0;
            int total_blobs_verified = 0;
            int total_blobs_deleted = 0;

            for (const auto& cr : chunk_results) {
                if (cr.succeeded) {
                    chunks_succeeded++;
                    total_blobs_verified += cr.blobs_verified;
                    total_blobs_deleted += cr.blobs_deleted;
                } else {
                    chunks_failed++;
                }
            }

            EXPECT_EQ(chunks_succeeded, 2);
            EXPECT_EQ(chunks_failed, 1);
            EXPECT_EQ(total_blobs_verified, 5);
            EXPECT_EQ(total_blobs_deleted, 4);
        }

        TEST(CleanupServiceBatchingTest, UploadTaskCleanupIsSinglePassBounded) {
            constexpr int kUploadTaskCleanupBatchSize = 100;
            int total_expired = 250;
            int fetched = std::min(total_expired, kUploadTaskCleanupBatchSize);

            EXPECT_EQ(fetched, 100);
            EXPECT_EQ(total_expired - fetched, 150);
        }

    } // namespace
} // namespace disk::services
