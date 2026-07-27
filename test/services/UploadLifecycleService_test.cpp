/**
 * @file UploadLifecycleService_test.cpp
 * @brief Upload lifecycle pure helper contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include "services/UploadLifecycleService.hpp"

#include <cstdint>
#include <optional>
#include <string>

#include <gtest/gtest.h>

namespace disk::upload {
    namespace {

        TEST(UploadLifecycleInitDecisionTest, ExistingContentWins) {
            auto decision = DecideInitFlow(true, "upload-id");
            EXPECT_EQ(decision.type, InitDecisionType::InstantUpload);
            EXPECT_TRUE(decision.upload_id.empty());
        }

        TEST(UploadLifecycleInitDecisionTest, ExistingTaskResumesWhenNoContent) {
            auto decision = DecideInitFlow(false, "upload-id");
            EXPECT_EQ(decision.type, InitDecisionType::ResumeUpload);
            EXPECT_EQ(decision.upload_id, "upload-id");
        }

        TEST(UploadLifecycleInitDecisionTest, MissingContentAndTaskStartsNewUpload) {
            auto decision = DecideInitFlow(false, "");
            EXPECT_EQ(decision.type, InitDecisionType::StartNewUpload);
            EXPECT_TRUE(decision.upload_id.empty());
        }

        TEST(UploadLifecycleChunkAcceptanceTest, AcceptsFullAndFinalShortChunks) {
            auto first_chunk = ValidateChunkAcceptance(0, 10, 25, 10, 3);
            ASSERT_TRUE(first_chunk.has_value());
            EXPECT_EQ(first_chunk->expected_size, 10);

            auto final_chunk = ValidateChunkAcceptance(2, 5, 25, 10, 3);
            ASSERT_TRUE(final_chunk.has_value());
            EXPECT_EQ(final_chunk->expected_size, 5);
        }

        TEST(UploadLifecycleChunkAcceptanceTest, RejectsOutOfRangeChunkIndex) {
            auto result = ValidateChunkAcceptance(3, 1, 25, 10, 3);
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().error.code, ErrorCode::ValidationFailed);
            EXPECT_EQ(result.error().error.message, "Chunk index out of range");
            EXPECT_EQ(result.error().expected_size, 0);
        }

        TEST(UploadLifecycleChunkAcceptanceTest, RejectsUnexpectedChunkSize) {
            auto result = ValidateChunkAcceptance(2, 6, 25, 10, 3);
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().error.code, ErrorCode::ValidationFailed);
            EXPECT_EQ(result.error().error.message, "Unexpected chunk size");
            EXPECT_EQ(result.error().expected_size, 5);
        }

        TEST(UploadLifecycleCoverageTest, CompleteCoverageRequiresCountAndMaxIndex) {
            EXPECT_TRUE(IsCompleteCoverage(3, ChunkCoverage{ .uploaded_count = 3, .max_chunk_index = 2 }));
            EXPECT_FALSE(IsCompleteCoverage(3, ChunkCoverage{ .uploaded_count = 2, .max_chunk_index = 2 }));
            EXPECT_FALSE(IsCompleteCoverage(3, ChunkCoverage{ .uploaded_count = 3, .max_chunk_index = 1 }));
            EXPECT_FALSE(IsCompleteCoverage(3, ChunkCoverage{ .uploaded_count = 3, .max_chunk_index = 3 }));
            EXPECT_FALSE(IsCompleteCoverage(3, ChunkCoverage{}));
        }

        TEST(UploadLifecycleCoverageTest, EmptyTaskCoverageRequiresZeroUploadedCount) {
            EXPECT_TRUE(IsCompleteCoverage(0, ChunkCoverage{ .uploaded_count = 0, .max_chunk_index = -1 }));
            EXPECT_FALSE(IsCompleteCoverage(0, ChunkCoverage{ .uploaded_count = 1, .max_chunk_index = 0 }));
        }

        TEST(UploadLifecycleFinalizeDecisionTest, ReusesExistingContentWhenPresent) {
            auto decision = DecideFinalizeStorage(42);
            EXPECT_EQ(decision.type, FinalizeStorageDecisionType::ReuseExistingContent);
            ASSERT_TRUE(decision.existing_content_id.has_value());
            EXPECT_EQ(decision.existing_content_id.value(), 42);
        }

        TEST(UploadLifecycleFinalizeDecisionTest, PromotesWhenContentMissing) {
            auto decision = DecideFinalizeStorage(std::nullopt);
            EXPECT_EQ(decision.type, FinalizeStorageDecisionType::PromoteAsNewContent);
            EXPECT_FALSE(decision.existing_content_id.has_value());
        }

        TEST(UploadLifecycleExpirationTest, RejectsUnboundedBatchBeforeDatabaseAccess) {
            UploadLifecycleService service(nullptr, nullptr, nullptr);

            auto empty = drogon::sync_wait(service.ExpireInProgressUploads(0));
            auto oversized = drogon::sync_wait(service.ExpireInProgressUploads(501));

            ASSERT_FALSE(empty.has_value());
            EXPECT_EQ(empty.error().code, ErrorCode::ValidationFailed);
            ASSERT_FALSE(oversized.has_value());
            EXPECT_EQ(oversized.error().code, ErrorCode::ValidationFailed);
        }

        TEST(UploadLifecycleExpirationTest, ContinuesOnFullCandidatePageDespiteCasLoss) {
            EXPECT_TRUE(ShouldContinueExpirationScan(100, 100));
            EXPECT_FALSE(ShouldContinueExpirationScan(99, 100));
            EXPECT_FALSE(ShouldContinueExpirationScan(0, 0));
        }

    } // namespace
} // namespace disk::upload
