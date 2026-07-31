#include "services/StorageWorkerRuntime.hpp"

#include <stdexcept>
#include <utility>

#include "utils/LogHelper.hpp"

namespace disk::jobs {

    StorageWorkerRuntime::StorageWorkerRuntime(
        RunCallback run_callback,
        StorageWorkerRuntimeOptions options
    ) : m_run_callback(std::move(run_callback)),
        m_options(options) {
        if (!m_run_callback) {
            throw std::invalid_argument("Storage worker runtime callback is required");
        }
        if (m_options.poll_interval_ms < 100 || m_options.poll_interval_ms > 60000) {
            throw std::invalid_argument(
                "Storage worker poll interval must be in range 100-60000 milliseconds"
            );
        }
    }

    auto StorageWorkerRuntime::Start(trantor::EventLoop* loop) -> void {
        if (loop == nullptr) {
            throw std::invalid_argument("Storage worker runtime event loop is required");
        }
        if (weak_from_this().expired()) {
            throw std::logic_error("Storage worker runtime must be owned by shared_ptr");
        }

        bool expected = false;
        if (!m_started.compare_exchange_strong(expected, true)) {
            return;
        }

        m_loop = loop;
        const auto weak_self = weak_from_this();
        const auto interval_seconds = static_cast<double>(m_options.poll_interval_ms) / 1000.0;
        m_poll_timer.store(loop->runEvery(interval_seconds, [weak_self]() {
            if (const auto self = weak_self.lock(); self != nullptr) {
                self->TriggerPoll();
            }
        }));
        loop->queueInLoop([weak_self]() {
            if (const auto self = weak_self.lock(); self != nullptr) {
                self->TriggerPoll();
            }
        });

        Logger::Info(utils::LogContext{ .operation = "storage_worker_runtime" })
            << "Storage worker runtime started: poll_interval_ms=" << m_options.poll_interval_ms;
    }

    auto StorageWorkerRuntime::BeginDrain() -> void {
        if (!m_accepting.exchange(false)) {
            return;
        }

        const auto timer = m_poll_timer.exchange(trantor::InvalidTimerId);
        if (m_loop != nullptr && timer != trantor::InvalidTimerId) {
            m_loop->queueInLoop([loop = m_loop, timer]() { loop->invalidateTimer(timer); });
        }
        Logger::Info(utils::LogContext{ .operation = "storage_worker_runtime" })
            << "Storage worker runtime draining: in_flight=" << m_poll_inflight.load();
    }

    auto StorageWorkerRuntime::PollOnce() -> drogon::Task<bool> {
        if (!m_accepting.load() || m_poll_inflight.exchange(true)) {
            co_return false;
        }
        if (!m_accepting.load()) {
            m_poll_inflight.store(false);
            co_return false;
        }

        while (m_accepting.load()) {
            try {
                auto result = co_await m_run_callback();
                if (!result) {
                    Logger::Error(utils::LogContext{ .operation = "storage_worker_poll" })
                        << "Storage worker poll failed";
                    break;
                }
                if (result->claimed == 0) {
                    break;
                }
                Logger::Info(utils::LogContext{ .operation = "storage_worker_poll" })
                    << "Storage worker poll completed: claimed=" << result->claimed
                    << ", succeeded=" << result->succeeded
                    << ", retried=" << result->retried
                    << ", dead_lettered=" << result->dead_lettered
                    << ", ownership_lost=" << result->ownership_lost;
            } catch (const std::exception&) {
                Logger::Error(utils::LogContext{ .operation = "storage_worker_poll" })
                    << "Storage worker poll threw";
                break;
            }
        }

        m_poll_inflight.store(false);
        co_return true;
    }

    auto StorageWorkerRuntime::IsDrained() const noexcept -> bool {
        return !m_poll_inflight.load();
    }

    auto StorageWorkerRuntime::TriggerPoll() -> void {
        const auto weak_self = weak_from_this();
        drogon::async_run([weak_self]() -> drogon::Task<void> {
            if (const auto self = weak_self.lock(); self != nullptr) {
                static_cast<void>(co_await self->PollOnce());
            }
        });
    }

} // namespace disk::jobs
