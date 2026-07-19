/**
 * @file UploadStateMachine.hpp
 * @brief Pure upload task state-machine rules.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace disk::upload {

    enum class UploadTaskStatus : int16_t {
        InProgress = 0,
        Completed = 1,
        Cancelled = 2,
        Expired = 3,
        Finalizing = 4,
        Failed = 5,
    };

    enum class FinalizeRequestAction {
        ClaimLease,
        TakeOverExpiredLease,
        RetryLater,
        ReplayCompleted,
        RejectTerminal,
    };

    enum class CancelRequestAction {
        Cancel,
        ReplayCancelled,
        RejectConflict,
        RejectTerminal,
    };

    [[nodiscard]] constexpr auto ToStorageValue(UploadTaskStatus status) -> int16_t {
        return static_cast<int16_t>(status);
    }

    [[nodiscard]] auto UploadTaskStatusFromStorage(int status) -> std::optional<UploadTaskStatus>;
    [[nodiscard]] auto IsTerminalStatus(UploadTaskStatus status) -> bool;
    [[nodiscard]] auto IsTerminalStatus(int status) -> bool;
    [[nodiscard]] auto IsAllowedTransition(UploadTaskStatus from, UploadTaskStatus to) -> bool;

    [[nodiscard]] auto DecideFinalizeRequest(int current_status, bool lease_expired)
        -> FinalizeRequestAction;
    [[nodiscard]] auto DecideCancelRequest(int current_status) -> CancelRequestAction;

    [[nodiscard]] auto CanRenewFinalizeLease(
        int current_status,
        std::string_view current_owner,
        uint64_t current_version,
        std::string_view requester,
        uint64_t expected_version
    ) -> bool;

    [[nodiscard]] auto CanCommitFinalizeLease(
        int current_status,
        std::string_view current_owner,
        uint64_t current_version,
        std::string_view requester,
        uint64_t expected_version
    ) -> bool;

    // Compatibility guards for current callers while persistence moves to CAS primitives.
    [[nodiscard]] auto CanComplete(int current_status) -> bool;
    [[nodiscard]] auto CanCancelOrExpire(int current_status) -> bool;

} // namespace disk::upload
