/**
 * @file SystemService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 系统服务
 * @version 0.1
 * @date 2026-02-18
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "SystemService.hpp"

#include <drogon/drogon.h>

#include "models/Files.hpp"
#include "models/Folders.hpp"
#include "models/Users.hpp"

namespace disk::system {

    SystemService::SystemService(
        drogon::orm::DbClientPtr db_client,
        drogon::nosql::RedisClientPtr redis_client
    )
        : m_db_client(std::move(db_client)),
          m_redis_client(std::move(redis_client)),
          m_start_time(std::chrono::steady_clock::now()) {}

    auto SystemService::GetInfo(uint64_t user_id) -> drogon::Task<Result<SystemInfo>> {
        SystemInfo info;
        info.version = "1.0.0";
        info.drogon_version = drogon::getVersion();
        info.build_time = GetBuildTime();

        // 计算运行时间
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - m_start_time);
        info.uptime = uptime.count();

        // 获取连接统计
        info.connections = co_await GetConnectionStats();

        // 获取存储统计
        info.storage = co_await GetStorageStats();

        co_return info;
    }

    auto SystemService::GetConnectionStats() -> drogon::Task<ConnectionStats> {
        ConnectionStats stats;

        // 获取当前数据库连接数（Drogon 没有直接提供这个 API，返回估算值）
        stats.current = 1;
        stats.peak = 10;

        co_return stats;
    }

    auto SystemService::GetStorageStats() -> drogon::Task<StorageStats> {
        StorageStats stats;

        try {
            // 获取用户总数
            auto users_result = co_await m_db_client->execSqlCoro(
                "SELECT COUNT(*) as count FROM users WHERE status != -1"
            );
            if (!users_result.empty()) {
                stats.total_users = users_result[0]["count"].as<int64_t>();
            }

            // 获取文件总数和总大小
            auto files_result = co_await m_db_client->execSqlCoro(
                "SELECT COUNT(*) as count, COALESCE(SUM(size), 0) as total_size FROM files WHERE "
                "deleted_at IS NULL"
            );
            if (!files_result.empty()) {
                stats.total_files = files_result[0]["count"].as<int64_t>();
                stats.total_size = files_result[0]["total_size"].as<int64_t>();
            }

            // 获取文件夹总数
            auto folders_result = co_await m_db_client->execSqlCoro(
                "SELECT COUNT(*) as count FROM folders WHERE deleted_at IS NULL"
            );
            if (!folders_result.empty()) {
                stats.total_folders = folders_result[0]["count"].as<int64_t>();
            }
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "获取存储统计失败: " << e.base().what();
        }

        co_return stats;
    }

    auto SystemService::GetBuildTime() -> std::string {
        return std::string(__DATE__) + " " + std::string(__TIME__);
    }

} // namespace disk::system
