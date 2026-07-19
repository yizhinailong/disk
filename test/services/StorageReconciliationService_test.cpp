#include "services/StorageReconciliationService.hpp"

#include <string>

#include <gtest/gtest.h>

namespace disk::reconciliation {
    namespace {
        TEST(StorageReconciliationContractTest, ParsesAllStableScopes) {
            EXPECT_EQ(ParseReconciliationScope("contents"), ReconciliationScope::Contents);
            EXPECT_EQ(ParseReconciliationScope("users"), ReconciliationScope::Users);
            EXPECT_EQ(ParseReconciliationScope("staging"), ReconciliationScope::Staging);
            EXPECT_EQ(ParseReconciliationScope("final"), ReconciliationScope::Final);
            EXPECT_FALSE(ParseReconciliationScope("unknown").has_value());
        }

        TEST(StorageReconciliationContractTest, ValidatesScopeSpecificCursorsAndBounds) {
            ReconciliationPageRequest database_request{
                .scan_id = "2026-07-19T12:00Z",
                .scope = ReconciliationScope::Contents,
                .after_id = 7,
                .limit = kMaxDatabaseReconciliationPageSize,
            };
            EXPECT_TRUE(ValidateReconciliationPageRequest(database_request).has_value());

            database_request.continuation_token = "opaque";
            EXPECT_FALSE(ValidateReconciliationPageRequest(database_request).has_value());

            ReconciliationPageRequest object_request{
                .scan_id = "scan_123",
                .scope = ReconciliationScope::Staging,
                .continuation_token = "opaque-token",
                .limit = kMaxObjectReconciliationPageSize,
            };
            EXPECT_TRUE(ValidateReconciliationPageRequest(object_request).has_value());

            object_request.after_id = 1;
            EXPECT_FALSE(ValidateReconciliationPageRequest(object_request).has_value());
            object_request.after_id = 0;
            object_request.limit = kMaxObjectReconciliationPageSize + 1;
            EXPECT_FALSE(ValidateReconciliationPageRequest(object_request).has_value());
        }

        TEST(StorageReconciliationContractTest, ObjectResourceIdsAreStableAndBounded) {
            const auto first = BuildObjectResourceId("staging/upload/chunks/0.part");
            const auto repeated = BuildObjectResourceId("staging/upload/chunks/0.part");
            const auto different = BuildObjectResourceId("staging/upload/chunks/1.part");

            EXPECT_EQ(first.size(), 64U);
            EXPECT_EQ(first, repeated);
            EXPECT_NE(first, different);
        }

        TEST(StorageReconciliationContractTest, OnlyProvenZeroReferencesAllowAutomaticGc) {
            EXPECT_TRUE(ShouldEnqueueBlobGc(0, 0));
            EXPECT_FALSE(ShouldEnqueueBlobGc(1, 0));
            EXPECT_FALSE(ShouldEnqueueBlobGc(0, 1));
            EXPECT_FALSE(ShouldEnqueueBlobGc(-1, 0));
        }
    } // namespace
} // namespace disk::reconciliation
