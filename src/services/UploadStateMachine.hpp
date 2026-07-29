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
    [[nodiscard]] auto UploadTaskStatusName(UploadTaskStatus status) noexcept -> std::string_view;
    [[nodiscard]] auto IsTerminalStatus(UploadTaskStatus status) -> bool;
    [[nodiscard]] auto IsTerminalStatus(int status) -> bool;

    [[nodiscard]] auto DecideFinalizeRequest(int current_status, bool lease_expired)
        -> FinalizeRequestAction;
    [[nodiscard]] auto DecideCancelRequest(int current_status) -> CancelRequestAction;

} // namespace disk::upload
