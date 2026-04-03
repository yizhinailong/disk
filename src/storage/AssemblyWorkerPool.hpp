/**
 * @file AssemblyWorkerPool.hpp
 * @brief Bounded worker pool for upload assembly I/O offload
 * @details Enforces backpressure on concurrent assembly operations:
 *          - Max concurrent: 4
 *          - Queue limit: 32 pending jobs
 *          - Overflow: fast-fail with error response
 * @author LiuFeng (liufeng.code@outlook.com)
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstddef>
#include <mutex>
#include <optional>

namespace disk::storage {

    /**
     * @brief Bounded two-tier worker pool for assembly I/O concurrency control
     *
     * Enforces backpressure with two tiers:
     *   - Running tier (m_running): up to MAX_CONCURRENT (4) jobs executing now
     *   - Pending tier (m_pending): up to QUEUE_LIMIT (32) jobs queued by the
     *     coroutine runtime (Drogon's thread pool scheduler provides natural queuing)
     *   - Overflow: fast-fail with error response
     *
     * Total capacity = 4 running + 32 pending = 36 concurrent slot holders.
     * Beyond 36, requests are immediately rejected.
     *
     * Thread-safe: all public methods are guarded by a mutex.
     */
    class AssemblyWorkerPool {
    public:
        class SlotGuard {
        public:
            SlotGuard() = default;

            ~SlotGuard() {
                if (m_pool != nullptr) {
                    m_pool->Release();
                }
            }

            SlotGuard(const SlotGuard&) = delete;
            auto operator=(const SlotGuard&) -> SlotGuard& = delete;

            SlotGuard(SlotGuard&& other) noexcept : m_pool(other.m_pool) { other.m_pool = nullptr; }

            auto operator=(SlotGuard&& other) noexcept -> SlotGuard& {
                if (this == &other) {
                    return *this;
                }
                if (m_pool != nullptr) {
                    m_pool->Release();
                }
                m_pool = other.m_pool;
                other.m_pool = nullptr;
                return *this;
            }

        private:
            friend class AssemblyWorkerPool;

            explicit SlotGuard(AssemblyWorkerPool* pool) : m_pool(pool) {}

            AssemblyWorkerPool* m_pool = nullptr;
        };

        static constexpr size_t MAX_CONCURRENT = 4;
        static constexpr size_t QUEUE_LIMIT = 32;

        /**
         * @brief Get the singleton instance
         * @return Reference to the global AssemblyWorkerPool
         */
        static auto GetInstance() -> AssemblyWorkerPool& {
            static AssemblyWorkerPool instance;
            return instance;
        }

        /**
         * @brief Try to acquire a slot for assembly work
         * @details First MAX_CONCURRENT callers get a "running" slot.
         *          Next QUEUE_LIMIT callers get a "pending" slot (queued by the
         *          coroutine runtime — in Drogon's thread pool model, pending jobs
         *          are naturally queued by the scheduler until a running slot frees up).
         *          Beyond that: fast-fail (returns false).
         * @return true if slot acquired (either running or pending),
         *         false if the pool is fully saturated (fast-fail)
         */
        auto TryAcquire() -> bool {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_running < MAX_CONCURRENT) {
                ++m_running;
                return true;
            }
            if (m_pending < QUEUE_LIMIT) {
                ++m_pending;
                return true;
            }
            return false; // fast-fail: pool saturated
        }

        auto TryAcquireGuard() -> std::optional<SlotGuard> {
            if (!TryAcquire()) {
                return std::nullopt;
            }
            return SlotGuard(this);
        }

        /**
         * @brief Release a slot after assembly work completes
         * @details Decrements m_pending first, then m_running. This naturally
         *          promotes queued jobs to running status as callers release slots.
         */
        auto Release() -> void {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_pending > 0) {
                --m_pending;
            } else if (m_running > 0) {
                --m_running;
            }
        }

        /**
         * @brief Get current number of running + pending assembly jobs
         * @return Count of all jobs holding a slot
         */
        auto ActiveCount() const -> size_t {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_running + m_pending;
        }

        /**
         * @brief Get current number of running assembly jobs
         * @return Count of jobs in the running tier
         */
        auto RunningCount() const -> size_t {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_running;
        }

        /**
         * @brief Get current number of pending assembly jobs
         * @return Count of jobs in the pending (queued) tier
         */
        auto PendingCount() const -> size_t {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_pending;
        }

        AssemblyWorkerPool(const AssemblyWorkerPool&) = delete;
        auto operator=(const AssemblyWorkerPool&) -> AssemblyWorkerPool& = delete;
        AssemblyWorkerPool(AssemblyWorkerPool&&) = delete;
        auto operator=(AssemblyWorkerPool&&) -> AssemblyWorkerPool& = delete;

    private:
        AssemblyWorkerPool() = default;

        mutable std::mutex m_mutex;
        size_t m_running = 0;
        size_t m_pending = 0;
    };

} // namespace disk::storage
