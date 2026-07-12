#include "services/QuotaService.hpp"

#include <gtest/gtest.h>

#include <type_traits>
#include <utility>

namespace disk::quota {
    namespace {

        TEST(QuotaServiceCompileTest, CanConstructWithNullDbClient) {
            QuotaService service(nullptr);
            SUCCEED();
        }

        TEST(QuotaServiceContractTest, ExposesCheckedUsedStorageAdjustmentForTransactions) {
            using CheckedAdjustResult = decltype(std::declval<QuotaService&>().AdjustUsedStorageChecked(
                std::declval<const drogon::orm::DbClientPtr&>(),
                uint64_t{ 1 },
                int64_t{ -1 }
            ));

            EXPECT_TRUE((std::is_same_v<CheckedAdjustResult, drogon::Task<Result<void>>>));
        }

        TEST(QuotaServiceContractTest, ReconciliationCarriesPersistedAndObservedAccounting) {
            AccountingReconciliation reconciliation;
            reconciliation.user_id = 7;
            reconciliation.storage_used = 100;
            reconciliation.storage_reserved = 20;
            reconciliation.storage_quota = 1000;
            reconciliation.active_file_bytes = 60;
            reconciliation.trash_item_bytes = 40;
            reconciliation.in_progress_reserved_bytes = 20;

            EXPECT_EQ(reconciliation.user_id, 7u);
            EXPECT_EQ(reconciliation.storage_used, 100u);
            EXPECT_EQ(reconciliation.storage_reserved, 20u);
            EXPECT_EQ(reconciliation.storage_quota, 1000u);
            EXPECT_EQ(reconciliation.active_file_bytes, 60u);
            EXPECT_EQ(reconciliation.trash_item_bytes, 40u);
            EXPECT_EQ(reconciliation.in_progress_reserved_bytes, 20u);
        }

    } ///< namespace
} ///< namespace disk::quota
