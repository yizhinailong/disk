/**
 * @file ScheduledTasks.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 定时任务管理器实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "services/ScheduledTasks.hpp"

namespace disk::services {

    // ==================== 初始化 ====================

    auto ScheduledTasks::Initialize(drogon::orm::DbClientPtr db_client) -> void {
        auto instance = GetInstance();
        if (!instance->m_cleanup_service) {
            instance->m_cleanup_service = std::make_shared<CleanupService>(std::move(db_client));
        }
    }

    // ==================== 任务注册 ====================

    auto ScheduledTasks::Register() -> void {
        auto instance = GetInstance();

        // async_func wraps the coroutine lambda in std::function<void()> that keeps
        // captures alive. runEvery stores the function for program lifetime.
        drogon::app().getLoop()->runEvery(
            3600.0,
            drogon::async_func([cleanup_service = instance->m_cleanup_service]() -> drogon::Task<void> {
                LOG_INFO << "Scheduled cleanup task started";
                const auto& service = cleanup_service;
                auto result = co_await service->CleanupExpiredTrash();
                if (!result) {
                    LOG_ERROR << "Scheduled cleanup task failed: " << result.error().message;
                }
            })
        );

        LOG_INFO << "Scheduled cleanup task registered (runs every hour)";
    }

} // namespace disk::services
