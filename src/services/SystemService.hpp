/**
 * @file SystemService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 系统服务
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

#include <json/json.h>

#include "utils/ErrorCode.hpp"

namespace disk::system {

    struct ConnectionStats {
        int64_t current{ 0 };
        int64_t peak{ 0 };
        int64_t db_pool_size{ 0 };
        int64_t redis_pool_size{ 0 };

        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["current"] = static_cast<Json::Int64>(current);
            json["peak"] = static_cast<Json::Int64>(peak);
            json["db_pool_size"] = static_cast<Json::Int64>(db_pool_size);
            json["redis_pool_size"] = static_cast<Json::Int64>(redis_pool_size);
            return json;
        }
    };

    struct StorageStats {
        int64_t total_users{ 0 };
        int64_t total_files{ 0 };
        int64_t total_folders{ 0 };
        int64_t total_size{ 0 };

        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["total_users"] = static_cast<Json::Int64>(total_users);
            json["total_files"] = static_cast<Json::Int64>(total_files);
            json["total_folders"] = static_cast<Json::Int64>(total_folders);
            json["total_size"] = static_cast<Json::Int64>(total_size);
            return json;
        }
    };

    struct SystemInfo {
        std::string version;
        std::string drogon_version;
        std::string build_time;
        int64_t uptime{ 0 };
        ConnectionStats connections;
        StorageStats storage;

        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["version"] = version;
            json["drogon_version"] = drogon_version;
            json["build_time"] = build_time;
            json["uptime"] = static_cast<Json::Int64>(uptime);
            json["connections"] = connections.ToJson();
            json["storage"] = storage.ToJson();
            return json;
        }
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
