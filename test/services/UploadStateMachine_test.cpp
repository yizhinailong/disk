/**
 * @file UploadStateMachine_test.cpp
 * @brief Upload state-machine contract tests.
 */

#include "services/UploadStateMachine.hpp"

#include <array>
#include <cstdint>

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

        template <typename Status>
        concept HasTerminalStatusClassifier = requires(Status status) {
            IsTerminalStatus(status);
        };

        static_assert(HasTerminalStatusClassifier<UploadTaskStatus>);
        static_assert(!HasTerminalStatusClassifier<int>);

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

    } // namespace
} // namespace disk::upload
