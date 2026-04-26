/**
 * @file HealthService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 健康检查服务
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "HealthService.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>

#include <drogon/orm/CoroMapper.h>
#include <drogon/utils/coroutine.h>

namespace disk::health {

    HealthService::HealthService(
        drogon::orm::DbClientPtr db_client,
        drogon::nosql::RedisClientPtr redis_client
    )
        : m_db_client(std::move(db_client)),
          m_redis_client(std::move(redis_client)),
          m_start_time(std::chrono::steady_clock::now()) {
        LOG_DEBUG << "HealthService initialization completed";
    }

    auto HealthService::Check() -> drogon::Task<HealthResult> {
        HealthResult result;
        result.version = "1.0.0";
        result.timestamp = GetTimestamp();

        // 计算运行时间
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - m_start_time);
        result.uptime = uptime.count();

        // 并行检查各组件：通过 co_future 启动 Redis 检查为独立异步任务，
        // 同时 co_await 数据库检查，两者在不同连接上交错执行
        auto check_start = std::chrono::steady_clock::now();
        auto redis_future = drogon::co_future(CheckRedis());
        auto db_status = co_await CheckDatabase();
        auto redis_status = redis_future.get();
        auto check_end = std::chrono::steady_clock::now();
        result.total_check_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(check_end - check_start).count();

        result.components["database"] = db_status;
        result.components["redis"] = redis_status;

        // 计算整体状态
        bool all_healthy = true;
        bool any_unhealthy = false;

        for (const auto& [name, status] : result.components) {
            if (status.status == "unhealthy") {
                any_unhealthy = true;
                all_healthy = false;
            } else if (status.status != "healthy") {
                all_healthy = false;
            }
        }

        if (all_healthy) {
            result.overall_status = "healthy";
        } else if (any_unhealthy) {
            result.overall_status = "unhealthy";
        } else {
            result.overall_status = "degraded";
        }

        co_return result;
    }

    auto HealthService::CheckDatabase() -> drogon::Task<ComponentStatus> {
        ComponentStatus status;
        status.status = "healthy";

        auto start = std::chrono::steady_clock::now();

        try {
            // 执行简单查询测试连接
            auto result = co_await m_db_client->execSqlCoro("SELECT 1");
            auto end = std::chrono::steady_clock::now();
            status.latency_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        } catch (const drogon::orm::DrogonDbException& e) {
            status.status = "unhealthy";
            status.message = e.base().what();
            status.latency_ms = 0;
        }

        co_return status;
    }

    auto HealthService::CheckRedis() -> drogon::Task<ComponentStatus> {
        ComponentStatus status;
        status.status = "healthy";

        if (!m_redis_client) {
            status.status = "unhealthy";
            status.message = "Redis client not configured";
            co_return status;
        }

        auto start = std::chrono::steady_clock::now();

        try {
            auto result = co_await m_redis_client->execCommandCoro("ping");
            auto end = std::chrono::steady_clock::now();
            status.latency_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

            if (!result.isNil() && result.asString() != "PONG") {
                status.status = "degraded";
                status.message = "Unexpected PING response";
            }
        } catch (const drogon::nosql::RedisException& e) {
            status.status = "unhealthy";
            status.message = e.what();
            status.latency_ms = 0;
        }

        co_return status;
    }

    auto HealthService::GetTimestamp() -> std::string {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        gmtime_r(&time_t, &tm);

        std::ostringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        return ss.str();
    }

} // namespace disk::health
