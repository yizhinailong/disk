/**
 * @file UploadStateMachine_test.cpp
 * @brief Upload state-machine contract tests.
 */

#include "services/UploadStateMachine.hpp"

#include <array>
#include <cstdint>
#include <string_view>

#include <gtest/gtest.h>

namespace disk::upload {
    namespace {

        constexpr std::array kStatuses{
            UploadTaskStatus::InProgress,
            UploadTaskStatus::Completed,
            UploadTaskStatus::Cancelled,
            UploadTaskStatus::Expired,
            UploadTaskStatus::Finalizing,
            UploadTaskStatus::Failed,
        };

        TEST(UploadStateMachineTest, StorageValuesMatchV003Schema) {
            EXPECT_EQ(ToStorageValue(UploadTaskStatus::InProgress), 0);
            EXPECT_EQ(ToStorageValue(UploadTaskStatus::Completed), 1);
            EXPECT_EQ(ToStorageValue(UploadTaskStatus::Cancelled), 2);
            EXPECT_EQ(ToStorageValue(UploadTaskStatus::Expired), 3);
            EXPECT_EQ(ToStorageValue(UploadTaskStatus::Finalizing), 4);
            EXPECT_EQ(ToStorageValue(UploadTaskStatus::Failed), 5);

            for (const auto status : kStatuses) {
                EXPECT_EQ(UploadTaskStatusFromStorage(ToStorageValue(status)), status);
            }
            EXPECT_FALSE(UploadTaskStatusFromStorage(-1).has_value());
            EXPECT_FALSE(UploadTaskStatusFromStorage(6).has_value());
        }

        TEST(UploadStateMachineTest, StatusNamesAreStableForDiagnostics) {
            EXPECT_EQ(UploadTaskStatusName(UploadTaskStatus::InProgress), "in_progress");
            EXPECT_EQ(UploadTaskStatusName(UploadTaskStatus::Completed), "completed");
            EXPECT_EQ(UploadTaskStatusName(UploadTaskStatus::Cancelled), "cancelled");
            EXPECT_EQ(UploadTaskStatusName(UploadTaskStatus::Expired), "expired");
            EXPECT_EQ(UploadTaskStatusName(UploadTaskStatus::Finalizing), "finalizing");
            EXPECT_EQ(UploadTaskStatusName(UploadTaskStatus::Failed), "failed");
        }

        TEST(UploadStateMachineTest, TerminalStatusesAreStable) {
            EXPECT_FALSE(IsTerminalStatus(UploadTaskStatus::InProgress));
            EXPECT_FALSE(IsTerminalStatus(UploadTaskStatus::Finalizing));
            EXPECT_TRUE(IsTerminalStatus(UploadTaskStatus::Completed));
            EXPECT_TRUE(IsTerminalStatus(UploadTaskStatus::Cancelled));
            EXPECT_TRUE(IsTerminalStatus(UploadTaskStatus::Expired));
            EXPECT_TRUE(IsTerminalStatus(UploadTaskStatus::Failed));
            EXPECT_FALSE(IsTerminalStatus(99));
        }

        TEST(UploadStateMachineTest, AllowsOnlyDocumentedStateTransitions) {
            for (const auto from : kStatuses) {
                for (const auto to : kStatuses) {
                    const bool expected =
                        (from == UploadTaskStatus::InProgress &&
                         (to == UploadTaskStatus::Finalizing ||
                          to == UploadTaskStatus::Cancelled ||
                          to == UploadTaskStatus::Expired)) ||
                        (from == UploadTaskStatus::Finalizing &&
                         (to == UploadTaskStatus::Completed ||
                          to == UploadTaskStatus::Failed));
                    EXPECT_EQ(IsAllowedTransition(from, to), expected)
                        << "from=" << ToStorageValue(from) << ", to=" << ToStorageValue(to);
                }
            }
        }

        TEST(UploadStateMachineTest, FinalizeDecisionCoversClaimTakeoverReplayAndRejection) {
            EXPECT_EQ(
                DecideFinalizeRequest(ToStorageValue(UploadTaskStatus::InProgress), false),
                FinalizeRequestAction::ClaimLease
            );
            EXPECT_EQ(
                DecideFinalizeRequest(ToStorageValue(UploadTaskStatus::Finalizing), false),
                FinalizeRequestAction::RetryLater
            );
            EXPECT_EQ(
                DecideFinalizeRequest(ToStorageValue(UploadTaskStatus::Finalizing), true),
                FinalizeRequestAction::TakeOverExpiredLease
            );
            EXPECT_EQ(
                DecideFinalizeRequest(ToStorageValue(UploadTaskStatus::Completed), false),
                FinalizeRequestAction::ReplayCompleted
            );

            for (const auto status : {
                     UploadTaskStatus::Cancelled,
                     UploadTaskStatus::Expired,
                     UploadTaskStatus::Failed,
                 }) {
                EXPECT_EQ(
                    DecideFinalizeRequest(ToStorageValue(status), true),
                    FinalizeRequestAction::RejectTerminal
                );
            }
            EXPECT_EQ(DecideFinalizeRequest(99, true), FinalizeRequestAction::RejectTerminal);
        }

        TEST(UploadStateMachineTest, CancelDecisionDistinguishesReplayConflictAndTerminal) {
            EXPECT_EQ(
                DecideCancelRequest(ToStorageValue(UploadTaskStatus::InProgress)),
                CancelRequestAction::Cancel
            );
            EXPECT_EQ(
                DecideCancelRequest(ToStorageValue(UploadTaskStatus::Cancelled)),
                CancelRequestAction::ReplayCancelled
            );
            EXPECT_EQ(
                DecideCancelRequest(ToStorageValue(UploadTaskStatus::Finalizing)),
                CancelRequestAction::RejectConflict
            );
            EXPECT_EQ(
                DecideCancelRequest(ToStorageValue(UploadTaskStatus::Completed)),
                CancelRequestAction::RejectConflict
            );
            EXPECT_EQ(
                DecideCancelRequest(ToStorageValue(UploadTaskStatus::Expired)),
                CancelRequestAction::RejectTerminal
            );
            EXPECT_EQ(
                DecideCancelRequest(ToStorageValue(UploadTaskStatus::Failed)),
                CancelRequestAction::RejectTerminal
            );
            EXPECT_EQ(DecideCancelRequest(99), CancelRequestAction::RejectTerminal);
        }

        TEST(UploadStateMachineTest, LeaseMutationRequiresCurrentOwnerAndVersion) {
            constexpr std::string_view current_owner = "api-a";
            constexpr uint64_t current_version = 7;
            const auto finalizing = ToStorageValue(UploadTaskStatus::Finalizing);

            EXPECT_TRUE(CanRenewFinalizeLease(
                finalizing,
                current_owner,
                current_version,
                current_owner,
                current_version
            ));
            EXPECT_FALSE(CanRenewFinalizeLease(
                finalizing,
                current_owner,
                current_version,
                "api-b",
                current_version
            ));
            EXPECT_FALSE(CanRenewFinalizeLease(
                finalizing,
                current_owner,
                current_version,
                "",
                current_version
            ));
            EXPECT_FALSE(CanRenewFinalizeLease(
                finalizing,
                current_owner,
                current_version,
                current_owner,
                current_version - 1
            ));
            EXPECT_FALSE(CanRenewFinalizeLease(
                ToStorageValue(UploadTaskStatus::Completed),
                current_owner,
                current_version,
                current_owner,
                current_version
            ));

            EXPECT_TRUE(CanCommitFinalizeLease(
                finalizing,
                current_owner,
                current_version,
                current_owner,
                current_version
            ));
            EXPECT_FALSE(CanCommitFinalizeLease(
                finalizing,
                current_owner,
                current_version,
                "api-b",
                current_version
            ));
            EXPECT_FALSE(CanCommitFinalizeLease(
                finalizing,
                current_owner,
                current_version,
                current_owner,
                current_version - 1
            ));
        }

        TEST(UploadStateMachineTest, CompatibilityGuardsOnlyAllowInProgress) {
            for (const auto status : kStatuses) {
                const bool expected = status == UploadTaskStatus::InProgress;
                EXPECT_EQ(CanComplete(ToStorageValue(status)), expected);
                EXPECT_EQ(CanCancelOrExpire(ToStorageValue(status)), expected);
            }
        }

    } // namespace
} // namespace disk::upload
