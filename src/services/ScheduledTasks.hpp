/**
 * @file ScheduledTasks.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 定时任务管理器
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <memory>

#include <drogon/orm/DbClient.h>

#include "services/CleanupService.hpp"
#include "utils/Singleton.hpp"

namespace disk::services {

    /**
     * @brief 定时任务管理器（单例）
     *
     * 管理系统定时任务，如回收站过期清理等。
     * 继承自 Singleton<ScheduledTasks>，确保全局只有一个实例。
     *
     * 使用方式：
     * @code
     * // 在应用启动时初始化（只需调用一次）
     * ScheduledTasks::Initialize(db_client);
     *
     * // 注册定时任务
     * ScheduledTasks::Register();
     * @endcode
     */
    class ScheduledTasks : public disk::utils::Singleton<ScheduledTasks> {
        friend class disk::utils::Singleton<ScheduledTasks>;

    public:
        /**
         * @brief 初始化 ScheduledTasks 单例
         * @param db_client 数据库客户端
         *
         * @note 此方法应在应用启动时调用一次。
         *       多次调用是安全的，但只有第一次调用有效。
         */
        static auto Initialize(drogon::orm::DbClientPtr db_client) -> void;

        /**
         * @brief 注册所有定时任务
         *
         * 注册以下定时任务：
         * - 每小时执行一次回收站过期清理
         *
         * @note 此方法应在 Initialize() 之后调用。
         */
        static auto Register() -> void;

        ~ScheduledTasks() = default;
        ScheduledTasks(const ScheduledTasks&) = delete;
        auto operator=(const ScheduledTasks&) -> ScheduledTasks& = delete;
        ScheduledTasks(ScheduledTasks&&) = delete;
        auto operator=(ScheduledTasks&&) -> ScheduledTasks& = delete;

    private:
        /**
         * @brief 私有构造函数（单例模式）
         */
        ScheduledTasks() = default;

        std::shared_ptr<CleanupService> m_cleanup_service;
    };

} // namespace disk::services
