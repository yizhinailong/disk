/**
 * @file StageTimer.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 轻量级 RAII 阶段计时器
 *
 * @details
 * 在析构时以 LOG_INFO 输出阶段名称和耗时（毫秒）。
 * 用于关键路径的性能观测，不改变任何业务逻辑。
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <chrono>
#include <string>
#include <utility>

#include "utils/LogHelper.hpp"

namespace disk::utils {

    /**
     * @brief 轻量级 RAII 阶段计时器
     *
     * 构造时记录起始时间戳，析构时输出 stage_name + duration_ms。
     * 典型用法：
     * @code
     * {
     *     StageTimer timer("chunk_scan");
     *     ///< ... 执行工作 ...
     * }   ///< <- 析构时自动输出日志
     * @endcode
     */
    class StageTimer {
    public:
        /**
         * @brief 构造计时器
         * @param stage_name 阶段名称，用于日志标识
         * @param log_context 调用方显式提供的类型化日志上下文
         */
        explicit StageTimer(
            std::string stage_name,
            LogContext log_context = {}
        ) noexcept
            : m_stage_name(std::move(stage_name)),
              m_log_context(std::move(log_context)),
              m_start(std::chrono::steady_clock::now()) {}

        ~StageTimer() {
            const auto end = std::chrono::steady_clock::now();
            const auto duration_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(end - m_start).count();
            Logger::Info(m_log_context)
                << "[stage_timer] " << m_stage_name << " duration_ms=" << duration_ms;
        }

        StageTimer(const StageTimer&) = delete;
        auto operator=(const StageTimer&) -> StageTimer& = delete;
        StageTimer(StageTimer&&) = delete;
        auto operator=(StageTimer&&) -> StageTimer& = delete;

    private:
        std::string m_stage_name;
        LogContext m_log_context;
        std::chrono::steady_clock::time_point m_start;
    };

} // namespace disk::utils
