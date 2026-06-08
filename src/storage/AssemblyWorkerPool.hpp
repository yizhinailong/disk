/**
 * @file AssemblyWorkerPool.hpp
 * @brief Assembly 组装并发控制池
 * @details 仅限制实际运行中的组装任务数量，并为同一 upload_id 提供单飞保护。
 * @author LiuFeng (liufeng.code@outlook.com)
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include "utils/ConfigMgr.hpp"

namespace disk::storage {

    /**
     * @brief Assembly 组装任务并发控制池
     *
     * 设计约束：
     * - 只统计真实运行中的组装任务（m_running）
     * - 最大并发数来自 ConfigMgr::GetAssemblyMaxConcurrent()
     * - 同一 upload_id 在进程内只允许一个组装任务运行
     * - 超出并发上限或命中单飞保护时立即失败，不做排队
     */
    class AssemblyWorkerPool {
    public:
        class SlotGuard {
        public:
            SlotGuard() = default;

            ~SlotGuard() {
                if (m_pool != nullptr) {
                    m_pool->Release(m_upload_id);
                }
            }

            SlotGuard(const SlotGuard&) = delete;
            auto operator=(const SlotGuard&) -> SlotGuard& = delete;

            SlotGuard(SlotGuard&& other) noexcept
                : m_pool(other.m_pool),
                  m_upload_id(std::move(other.m_upload_id)) {
                other.m_pool = nullptr;
                other.m_upload_id.clear();
            }

            auto operator=(SlotGuard&& other) noexcept -> SlotGuard& {
                if (this == &other) {
                    return *this;
                }
                if (m_pool != nullptr) {
                    m_pool->Release(m_upload_id);
                }
                m_pool = other.m_pool;
                m_upload_id = std::move(other.m_upload_id);
                other.m_pool = nullptr;
                other.m_upload_id.clear();
                return *this;
            }

        private:
            friend class AssemblyWorkerPool;

            SlotGuard(AssemblyWorkerPool* pool, std::string upload_id)
                : m_pool(pool),
                  m_upload_id(std::move(upload_id)) {}

            AssemblyWorkerPool* m_pool = nullptr;
            std::string m_upload_id;
        };

        static constexpr size_t DEFAULT_MAX_CONCURRENT = 4;

        /**
         * @brief Get the singleton instance
         * @return Reference to the global AssemblyWorkerPool
         */
        static auto GetInstance() -> AssemblyWorkerPool& {
            static AssemblyWorkerPool instance;
            return instance;
        }

        /**
         * @brief 尝试获取组装运行槽位
         * @param upload_id 上传任务 ID
         * @return 成功返回槽位守卫，失败返回 std::nullopt
         */
        [[nodiscard]]
        auto TryAcquire(const std::string& upload_id) -> std::optional<SlotGuard> {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_active_upload_ids.contains(upload_id)) {
                return std::nullopt;
            }

            if (m_running >= m_max_concurrent) {
                return std::nullopt;
            }

            ++m_running;
            m_active_upload_ids.insert(upload_id);
            return SlotGuard(this, upload_id);
        }

        /**
         * @brief 尝试获取组装运行槽位守卫
         * @param upload_id 上传任务 ID
         * @return 成功返回槽位守卫，失败返回 std::nullopt
         */
        [[nodiscard]]
        auto TryAcquireGuard(const std::string& upload_id) -> std::optional<SlotGuard> {
            return TryAcquire(upload_id);
        }

        /**
         * @brief 获取当前运行中的组装任务数量
         * @return 运行中的任务数
         */
        [[nodiscard]]
        auto ActiveCount() const -> size_t {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_running;
        }

        /**
         * @brief 获取当前运行中的组装任务数量
         * @return 运行中的任务数
         */
        [[nodiscard]]
        auto RunningCount() const -> size_t {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_running;
        }

        /**
         * @brief 获取配置的组装最大并发数
         * @return 最大并发数
         */
        [[nodiscard]]
        auto MaxConcurrent() const -> size_t {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_max_concurrent;
        }

        /**
         * @brief 判断指定 upload_id 是否正在组装
         * @param upload_id 上传任务 ID
         * @return true 表示正在组装，false 表示未在组装
         */
        [[nodiscard]]
        auto IsUploadActive(const std::string& upload_id) const -> bool {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_active_upload_ids.contains(upload_id);
        }

        AssemblyWorkerPool(const AssemblyWorkerPool&) = delete;
        ~AssemblyWorkerPool() = default;
        auto operator=(const AssemblyWorkerPool&) -> AssemblyWorkerPool& = delete;
        AssemblyWorkerPool(AssemblyWorkerPool&&) = delete;
        auto operator=(AssemblyWorkerPool&&) -> AssemblyWorkerPool& = delete;

    private:
        AssemblyWorkerPool() {
            const auto configured_max =
                static_cast<size_t>(disk::utils::ConfigMgr::GetInstance()->GetAssemblyMaxConcurrent());
            m_max_concurrent = configured_max == 0 ? DEFAULT_MAX_CONCURRENT : configured_max;
        }

        auto Release(const std::string& upload_id) -> void {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_running > 0) {
                --m_running;
            }
            m_active_upload_ids.erase(upload_id);
        }

        mutable std::mutex m_mutex;
        size_t m_max_concurrent = DEFAULT_MAX_CONCURRENT;
        size_t m_running = 0;
        std::unordered_set<std::string> m_active_upload_ids;
    };

} ///< namespace disk::storage
