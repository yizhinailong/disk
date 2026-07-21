/**
 * @file AssemblyConcurrencyLimiter.hpp
 * @brief 进程内 Assembly 组装并发限流器
 * @details 只限制本机在途组装数量；上传完成所有权由 PostgreSQL 租约决定。
 * @author LiuFeng (liufeng.code@outlook.com)
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <utility>

#include "utils/ConfigMgr.hpp"

namespace disk::storage {

    /**
     * @brief 本机 Assembly 组装并发限流器
     *
     * 设计约束：
     * - 只统计当前进程已接纳的组装操作
     * - 最大并发数来自 ConfigMgr::GetAssemblyMaxConcurrent()
     * - 不接收或保存 upload_id，不承担任务所有权或同任务排他
     * - 超出并发上限时立即失败，不在此处排队
     */
    class AssemblyConcurrencyLimiter {
    public:
        class SlotGuard {
        public:
            SlotGuard() = default;

            ~SlotGuard() {
                Reset();
            }

            SlotGuard(const SlotGuard&) = delete;
            auto operator=(const SlotGuard&) -> SlotGuard& = delete;

            SlotGuard(SlotGuard&& other) noexcept
                : m_limiter(std::exchange(other.m_limiter, nullptr)) {}

            auto operator=(SlotGuard&& other) noexcept -> SlotGuard& {
                if (this == &other) {
                    return *this;
                }

                Reset();
                m_limiter = std::exchange(other.m_limiter, nullptr);
                return *this;
            }

        private:
            friend class AssemblyConcurrencyLimiter;

            explicit SlotGuard(AssemblyConcurrencyLimiter* limiter)
                : m_limiter(limiter) {}

            auto Reset() -> void {
                if (m_limiter != nullptr) {
                    m_limiter->Release();
                    m_limiter = nullptr;
                }
            }

            AssemblyConcurrencyLimiter* m_limiter = nullptr;
        };

        static constexpr size_t DEFAULT_MAX_CONCURRENT = 4;

        static auto GetInstance() -> AssemblyConcurrencyLimiter& {
            static AssemblyConcurrencyLimiter instance;
            return instance;
        }

        [[nodiscard]]
        auto TryAcquire() -> std::optional<SlotGuard> {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_running >= m_max_concurrent) {
                return std::nullopt;
            }

            ++m_running;
            return SlotGuard(this);
        }

        [[nodiscard]]
        auto RunningCount() const -> size_t {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_running;
        }

        [[nodiscard]]
        auto MaxConcurrent() const -> size_t {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_max_concurrent;
        }

        AssemblyConcurrencyLimiter(const AssemblyConcurrencyLimiter&) = delete;
        ~AssemblyConcurrencyLimiter() = default;
        auto operator=(const AssemblyConcurrencyLimiter&) -> AssemblyConcurrencyLimiter& = delete;
        AssemblyConcurrencyLimiter(AssemblyConcurrencyLimiter&&) = delete;
        auto operator=(AssemblyConcurrencyLimiter&&) -> AssemblyConcurrencyLimiter& = delete;

    private:
        AssemblyConcurrencyLimiter() {
            const auto configured_max =
                static_cast<size_t>(disk::utils::ConfigMgr::GetInstance()->GetAssemblyMaxConcurrent());
            m_max_concurrent = configured_max == 0 ? DEFAULT_MAX_CONCURRENT : configured_max;
        }

        auto Release() -> void {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_running > 0) {
                --m_running;
            }
        }

        mutable std::mutex m_mutex;
        size_t m_max_concurrent = DEFAULT_MAX_CONCURRENT;
        size_t m_running = 0;
    };

} // namespace disk::storage
