/**
 * @file SystemService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 系统服务
 * @version 0.1
 * @date 2026-02-18
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include <drogon/nosql/RedisClient.h>
#include <drogon/orm/DbClient.h>

#include "utils/ErrorCode.hpp"

namespace disk::system {

    struct ConnectionStats {
        int64_t current{ 0 };
        int64_t peak{ 0 };
    };

    struct StorageStats {
        int64_t total_users{ 0 };
        int64_t total_files{ 0 };
        int64_t total_folders{ 0 };
        int64_t total_size{ 0 };
    };

    struct SystemInfo {
        std::string version;
        std::string drogon_version;
        std::string build_time;
        int64_t uptime{ 0 };
        ConnectionStats connections;
        StorageStats storage;
    };

    class SystemService {
    public:
        SystemService(
            drogon::orm::DbClientPtr db_client,
            drogon::nosql::RedisClientPtr redis_client
        );

        [[nodiscard]]
        auto GetInfo(uint64_t user_id) -> drogon::Task<Result<SystemInfo>>;

    private:
        [[nodiscard]]
        auto GetConnectionStats() -> drogon::Task<ConnectionStats>;

        [[nodiscard]]
        auto GetStorageStats() -> drogon::Task<StorageStats>;

        static auto GetBuildTime() -> std::string;

        drogon::orm::DbClientPtr m_db_client;
        drogon::nosql::RedisClientPtr m_redis_client;
        std::chrono::steady_clock::time_point m_start_time;
    };

} // namespace disk::system
