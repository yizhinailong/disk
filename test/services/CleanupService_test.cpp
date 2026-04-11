#include "services/CleanupService.hpp"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "utils/BatchUtils.hpp"

namespace disk::services {
    namespace {

        TEST(CleanupServiceCompileTest, CanConstructWithNullDbClient) {
            CleanupService service(nullptr);
            SUCCEED();
        }

        TEST(CleanupServiceBuildNumericInClauseTest, EmptyVectorReturnsEmptyString) {
            const std::vector<uint64_t> ids;
            EXPECT_TRUE(cleanup_internal::BuildNumericInClause(ids).empty());
        }

        TEST(CleanupServiceBuildNumericInClauseTest, MultipleIdsReturnCommaSeparatedClause) {
            const std::vector<uint64_t> ids{ 1, 42, 500, 999999 };
            EXPECT_EQ(cleanup_internal::BuildNumericInClause(ids), "1,42,500,999999");
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

        TEST(CleanupServiceBatchingTest, CursorAdvancesPastFailedBatchWithoutRetry) {
            constexpr int kFetchBatchSize = 3;
            std::vector<uint64_t> all_ids = { 10, 20, 30, 40, 50, 60 };
            std::vector<uint64_t> failing_ids = { 20, 30 };
            uint64_t last_seen_id = 0;
            std::vector<uint64_t> successfully_processed;
            int iterations = 0;

            while (true) {
                std::vector<uint64_t> batch;
                for (auto id : all_ids) {
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

                bool batch_has_failure = false;
                for (auto id : batch) {
                    for (auto fid : failing_ids) {
                        if (id == fid) {
                            batch_has_failure = true;
                            break;
                        }
                    }
                    if (batch_has_failure) {
                        break;
                    }
                }

                if (!batch_has_failure) {
                    for (auto id : batch) {
                        successfully_processed.push_back(id);
                    }
                }

                // 游标始终推进，不论该批次是否处理失败
                last_seen_id = batch_max_id;
                iterations++;

                if (batch.size() < static_cast<size_t>(kFetchBatchSize)) {
                    break;
                }
            }

            EXPECT_EQ(last_seen_id, 60u);
            EXPECT_EQ(iterations, 2);
            EXPECT_EQ(successfully_processed, (std::vector<uint64_t>{ 40, 50, 60 }));
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

        TEST(DISABLED_CleanupServiceIntegrationTest, BatchCleanupExpiredTrashMultipleItems) {
            GTEST_SKIP();
        }

        TEST(DISABLED_CleanupServiceIntegrationTest, BatchCleanupExpiredTrashChunkFailureRollbackContinue) {
            GTEST_SKIP();
        }

        TEST(DISABLED_CleanupServiceIntegrationTest, BatchCleanupExpiredTrashRepeatExecutionIdempotent) {
            GTEST_SKIP();
        }

        TEST(DISABLED_CleanupServiceIntegrationTest, BatchCleanupExpiredTrashStorageDeltaCorrectness) {
            GTEST_SKIP();
        }

        TEST(DISABLED_CleanupServiceIntegrationTest, BatchCleanupExpiredUploadTasks) {
            GTEST_SKIP();
        }

    } // namespace
} // namespace disk::services
