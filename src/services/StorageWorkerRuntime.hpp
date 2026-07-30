#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <drogon/utils/coroutine.h>
#include <trantor/net/EventLoop.h>

#include "services/StorageJobWorker.hpp"

namespace disk::jobs {

    struct StorageWorkerRuntimeOptions {
        uint32_t poll_interval_ms{ 1000 };
    };

    class StorageWorkerRuntime final : public std::enable_shared_from_this<StorageWorkerRuntime> {
    public:
        using RunCallback =
            std::function<drogon::Task<Result<StorageJobRunResult>>()>;

        StorageWorkerRuntime(
            std::string instance_id,
            RunCallback run_callback,
            StorageWorkerRuntimeOptions options = {}
        );

        auto Start(trantor::EventLoop* loop) -> void;

        auto BeginDrain() -> void;

        [[nodiscard]]
        auto PollOnce() -> drogon::Task<bool>;

        [[nodiscard]]
        auto IsDrained() const noexcept -> bool;

    private:
        auto TriggerPoll() -> void;

        RunCallback m_run_callback;
        StorageWorkerRuntimeOptions m_options;
        trantor::EventLoop* m_loop{};
        std::atomic<trantor::TimerId> m_poll_timer{ trantor::InvalidTimerId };
        std::atomic_bool m_started{ false };
        std::atomic_bool m_accepting{ true };
        std::atomic_bool m_poll_inflight{ false };
    };

} // namespace disk::jobs
