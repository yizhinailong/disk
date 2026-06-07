/**
 * @file HealthService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 健康检查服务
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <string>

#include <drogon/nosql/RedisClient.h>
#include <drogon/orm/DbClient.h>
#include <json/json.h>

namespace disk::health {

    // ==================== 数据结构 ====================

    struct ComponentStatus {
        std::string status;      // "healthy" or "unhealthy"
        std::string message;     // 错误信息（可选）
        int64_t latency_ms{ 0 }; // 响应延迟（毫秒）

        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["status"] = status;
            if (!message.empty()) {
                json["message"] = message;
            }
            json["latency_ms"] = static_cast<Json::Int64>(latency_ms);
            return json;
        }
    };

    struct HealthResult {
        std::string overall_status;                        // "healthy", "degraded", "unhealthy"
        std::string version;                               // 系统版本
        int64_t uptime{ 0 };                               // 运行时间（秒）
        std::string timestamp;                             // ISO 8601 时间戳
        int64_t total_check_ms{ 0 };                       // 健康检查总耗时（毫秒）
        std::map<std::string, ComponentStatus> components; // 各组件状态

        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["overall_status"] = overall_status;
            json["version"] = version;
            json["uptime"] = static_cast<Json::Int64>(uptime);
            json["total_check_ms"] = static_cast<Json::Int64>(total_check_ms);
            json["timestamp"] = timestamp;

            Json::Value components_json;
            for (const auto& [name, status] : components) {
                components_json[name] = status.ToJson();
            }
            json["components"] = components_json;
            return json;
        }
    };

    // ==================== Service ====================

    class HealthService {
    public:
        HealthService(
            drogon::orm::DbClientPtr db_client,
            drogon::nosql::RedisClientPtr redis_client
        );

        [[nodiscard]]
        auto Check() -> drogon::Task<HealthResult>;

    private:
        [[nodiscard]]
        auto CheckDatabase() -> drogon::Task<ComponentStatus>;

        [[nodiscard]]
        auto CheckRedis() -> drogon::Task<ComponentStatus>;

        static auto GetTimestamp() -> std::string;

        drogon::orm::DbClientPtr m_db_client;
        drogon::nosql::RedisClientPtr m_redis_client;
        std::chrono::steady_clock::time_point m_start_time;
    };

} // namespace disk::health
