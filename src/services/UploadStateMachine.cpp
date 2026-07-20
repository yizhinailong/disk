/**
 * @file UploadStateMachine.cpp
 * @brief Pure upload task state-machine rules.
 */

#include "UploadStateMachine.hpp"

namespace disk::upload {

    auto UploadTaskStatusFromStorage(int status) -> std::optional<UploadTaskStatus> {
        switch (status) {
            case ToStorageValue(UploadTaskStatus::InProgress):
                return UploadTaskStatus::InProgress;
            case ToStorageValue(UploadTaskStatus::Completed):
                return UploadTaskStatus::Completed;
            case ToStorageValue(UploadTaskStatus::Cancelled):
                return UploadTaskStatus::Cancelled;
            case ToStorageValue(UploadTaskStatus::Expired):
                return UploadTaskStatus::Expired;
            case ToStorageValue(UploadTaskStatus::Finalizing):
                return UploadTaskStatus::Finalizing;
            case ToStorageValue(UploadTaskStatus::Failed):
                return UploadTaskStatus::Failed;
            default:
                return std::nullopt;
        }
    }

    auto UploadTaskStatusName(UploadTaskStatus status) noexcept -> std::string_view {
        switch (status) {
            case UploadTaskStatus::InProgress:
                return "in_progress";
            case UploadTaskStatus::Completed:
                return "completed";
            case UploadTaskStatus::Cancelled:
                return "cancelled";
            case UploadTaskStatus::Expired:
                return "expired";
            case UploadTaskStatus::Finalizing:
                return "finalizing";
            case UploadTaskStatus::Failed:
                return "failed";
        }
        return "unknown";
    }

    auto IsTerminalStatus(UploadTaskStatus status) -> bool {
        switch (status) {
            case UploadTaskStatus::Completed:
            case UploadTaskStatus::Cancelled:
            case UploadTaskStatus::Expired:
            case UploadTaskStatus::Failed:
                return true;
            case UploadTaskStatus::InProgress:
            case UploadTaskStatus::Finalizing:
                return false;
        }
        return false;
    }

    auto IsTerminalStatus(int status) -> bool {
        const auto parsed = UploadTaskStatusFromStorage(status);
        return parsed.has_value() && IsTerminalStatus(parsed.value());
    }

    auto IsAllowedTransition(UploadTaskStatus from, UploadTaskStatus to) -> bool {
        switch (from) {
            case UploadTaskStatus::InProgress:
                return to == UploadTaskStatus::Finalizing ||
                       to == UploadTaskStatus::Cancelled ||
                       to == UploadTaskStatus::Expired;
            case UploadTaskStatus::Finalizing:
                return to == UploadTaskStatus::Completed || to == UploadTaskStatus::Failed;
            case UploadTaskStatus::Completed:
            case UploadTaskStatus::Cancelled:
            case UploadTaskStatus::Expired:
            case UploadTaskStatus::Failed:
                return false;
        }
        return false;
    }

    auto DecideFinalizeRequest(int current_status, bool lease_expired)
        -> FinalizeRequestAction {
        const auto parsed = UploadTaskStatusFromStorage(current_status);
        if (!parsed.has_value()) {
            return FinalizeRequestAction::RejectTerminal;
        }

        switch (parsed.value()) {
            case UploadTaskStatus::InProgress:
                return FinalizeRequestAction::ClaimLease;
            case UploadTaskStatus::Finalizing:
                return lease_expired ? FinalizeRequestAction::TakeOverExpiredLease : FinalizeRequestAction::RetryLater;
            case UploadTaskStatus::Completed:
                return FinalizeRequestAction::ReplayCompleted;
            case UploadTaskStatus::Cancelled:
            case UploadTaskStatus::Expired:
            case UploadTaskStatus::Failed:
                return FinalizeRequestAction::RejectTerminal;
        }
        return FinalizeRequestAction::RejectTerminal;
    }

    auto DecideCancelRequest(int current_status) -> CancelRequestAction {
        const auto parsed = UploadTaskStatusFromStorage(current_status);
        if (!parsed.has_value()) {
            return CancelRequestAction::RejectTerminal;
        }

        switch (parsed.value()) {
            case UploadTaskStatus::InProgress:
                return CancelRequestAction::Cancel;
            case UploadTaskStatus::Cancelled:
                return CancelRequestAction::ReplayCancelled;
            case UploadTaskStatus::Finalizing:
            case UploadTaskStatus::Completed:
                return CancelRequestAction::RejectConflict;
            case UploadTaskStatus::Expired:
            case UploadTaskStatus::Failed:
                return CancelRequestAction::RejectTerminal;
        }
        return CancelRequestAction::RejectTerminal;
    }

    auto CanRenewFinalizeLease(
        int current_status,
        std::string_view current_owner,
        uint64_t current_version,
        std::string_view requester,
        uint64_t expected_version
    ) -> bool {
        return current_status == ToStorageValue(UploadTaskStatus::Finalizing) &&
               !requester.empty() && current_owner == requester &&
               current_version == expected_version;
    }

    auto CanCommitFinalizeLease(
        int current_status,
        std::string_view current_owner,
        uint64_t current_version,
        std::string_view requester,
        uint64_t expected_version
    ) -> bool {
        return CanRenewFinalizeLease(
            current_status,
            current_owner,
            current_version,
            requester,
            expected_version
        );
    }

    auto CanComplete(int current_status) -> bool {
        return DecideFinalizeRequest(current_status, false) == FinalizeRequestAction::ClaimLease;
    }

    auto CanCancelOrExpire(int current_status) -> bool {
        return current_status == ToStorageValue(UploadTaskStatus::InProgress);
    }

} // namespace disk::upload
