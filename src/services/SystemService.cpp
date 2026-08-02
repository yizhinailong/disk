/**
 * @file SystemService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 系统服务
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "SystemService.hpp"

#include <drogon/drogon.h>

#include "models/Files.hpp"
#include "models/Folders.hpp"
#include "models/Users.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/StageTimer.hpp"

namespace disk::system {

    using disk::utils::ConfigMgr;
    using disk::utils::StageTimer;

    namespace {
        [[nodiscard]] auto GetBuildTime() -> std::string {
            return std::string(__DATE__) + " " + std::string(__TIME__);
        }

        [[nodiscard]] auto GetConnectionStats() -> drogon::Task<ConnectionStats> {
            ConnectionStats stats;

            auto config = ConfigMgr::GetInstance();
            stats.db_pool_size = config->GetDbPoolSize();
            stats.redis_pool_size = config->GetRedisPoolSize();

            /// Drogon 不暴露运行时活跃连接数，使用配置池大小作为上限估算
            stats.current = stats.db_pool_size;
            stats.peak = stats.db_pool_size;

            co_return stats;
        }
    } // namespace

    SystemService::SystemService(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)),
          m_start_time(std::chrono::steady_clock::now()) {
        Logger::Debug(disk::utils::ServiceRuntimeLogContext()) << "Service initialized: service=system";
    }

    auto SystemService::GetInfo(disk::utils::LogContext log_context)
        -> drogon::Task<Result<SystemInfo>> {
        StageTimer timer("system_get_info", log_context);

        SystemInfo info;
        info.version = "1.0.0";
        info.drogon_version = drogon::getVersion();
        info.build_time = GetBuildTime();

        /// 计算运行时间
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - m_start_time);
        info.uptime = uptime.count();

        /// 获取连接统计
        info.connections = co_await GetConnectionStats();

        /// 获取存储统计
        info.storage = co_await GetStorageStats(log_context);

        co_return info;
    }

    auto SystemService::GetStorageStats(disk::utils::LogContext log_context)
        -> drogon::Task<StorageStats> {
        StorageStats stats;

        try {
            /// 获取用户总数
            auto users_result = co_await m_db_client->execSqlCoro(
                "SELECT COUNT(*) as count FROM users WHERE status != -1"
            );
            if (!users_result.empty()) {
                stats.total_users = users_result[0]["count"].as<int64_t>();
            }

            /// 获取文件总数和总大小
            /// files 表只含活跃数据（删除操作会将行移至 trash 表），无需 WHERE 过滤
            auto files_result =
                co_await m_db_client->execSqlCoro(
                    "SELECT COUNT(*) as count, COALESCE(SUM(size), 0) as total_size FROM files"
                );
            if (!files_result.empty()) {
                stats.total_files = files_result[0]["count"].as<int64_t>();
                stats.total_size = files_result[0]["total_size"].as<int64_t>();
            }

            /// 获取文件夹总数
            /// folders 表只含活跃数据（删除操作会将行移至 trash 表），无需 WHERE 过滤
            auto folders_result = co_await m_db_client->execSqlCoro(
                "SELECT COUNT(*) as count FROM folders"
            );
            if (!folders_result.empty()) {
                stats.total_folders = folders_result[0]["count"].as<int64_t>();
            }
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error(log_context) << "Failed to get storage stats: " << e.base().what();
        }

        co_return stats;
    }

} // namespace disk::system
