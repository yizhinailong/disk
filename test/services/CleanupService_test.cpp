#include "services/CleanupService.hpp"

#include <gtest/gtest.h>

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
