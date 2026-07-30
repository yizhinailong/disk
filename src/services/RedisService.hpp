/**
 * @file RedisService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Redis 服务封装
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <string>

#include <drogon/nosql/RedisClient.h>

#include "utils/ErrorCode.hpp"
#include "utils/LogHelper.hpp"
#include "utils/Singleton.hpp"

namespace disk::services {

    /**
     * @brief Redis 服务类（单例）
     *
     * 封装 Redis 客户端操作，提供通用 Redis 接口
     * 继承自 Singleton<RedisService>，确保全局只有一个实例
     *
     * 使用方式：
     * @code
     * ///< 在应用启动时初始化（只需调用一次）
     * RedisService::Initialize(redis_client);
     *
     * ///< 在任何地方获取实例
     * auto redis = RedisService::GetInstance();
     * co_await redis->Set("key", "value");
     * @endcode
     */
    class RedisService : public disk::utils::Singleton<RedisService> {
        friend class disk::utils::Singleton<RedisService>;

    public:
        /**
         * @brief 初始化 RedisService 单例
         * @param redis_client Redis 客户端
         *
         * @note 此方法应在应用启动时调用一次。
         *       多次调用是安全的，但只有第一次调用有效。
         */
        static auto Initialize(drogon::nosql::RedisClientPtr redis_client) -> void;

        ~RedisService() = default;
        RedisService(const RedisService&) = delete;
        auto operator=(const RedisService&) -> RedisService& = delete;
        RedisService(RedisService&&) = delete;
        auto operator=(RedisService&&) -> RedisService& = delete;
        /// ==================== 通用方法 ====================

        /**
         * @brief 检查 Redis 服务是否返回 PONG
         * @return Result<bool> 成功返回响应是否有效，连接故障返回错误
         */
        [[nodiscard]]
        auto Ping(disk::utils::LogContext log_context = {}) -> drogon::Task<Result<bool>>;

        /**
         * @brief 设置 Redis 键值对
         * @param key Redis 键
         * @param value Redis 值
         * @param ttl 过期时间（秒），0 表示不过期
         * @return Result<void> 成功返回 void，失败返回错误
         */
        [[nodiscard]]
        auto Set(
            const std::string& key,
            const std::string& value,
            int ttl = 0,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<void>>;

        /**
         * @brief 获取 Redis 键对应的值
         * @param key Redis 键
         * @return Result<std::string> 成功返回值，失败返回错误
         */
        [[nodiscard]]
        auto Get(const std::string& key, disk::utils::LogContext log_context = {})
            -> drogon::Task<Result<std::string>>;

        /**
         * @brief 删除 Redis 键
         * @param key Redis 键
         * @return Result<void> 成功返回 void，失败返回错误
         */
        [[nodiscard]]
        auto Delete(const std::string& key, disk::utils::LogContext log_context = {})
            -> drogon::Task<Result<void>>;

        /**
         * @brief 检查 Redis 键是否存在
         * @param key Redis 键
         * @return Result<bool> 成功时返回是否存在，Redis 故障返回错误
         */
        [[nodiscard]]
        auto Exists(const std::string& key, disk::utils::LogContext log_context = {})
            -> drogon::Task<Result<bool>>;

        /**
         * @brief 原子性递增 Redis 键值（递增 1）
         * @param key Redis 键
         * @return Result<std::int64_t> 成功返回递增后的新值，失败返回错误
         */
        [[nodiscard]]
        auto Incr(const std::string& key, disk::utils::LogContext log_context = {})
            -> drogon::Task<Result<std::int64_t>>;

        /**
         * @brief 原子性比较并交换 (Compare-And-Swap)
         *
         * 使用 Lua 脚本实现原子 CAS 操作：
         * 1. 获取当前值
         * 2. 如果当前值等于期望值，则设置新值并返回 true
         * 3. 如果当前值不等于期望值，返回 false
         *
         * @param key Redis 键
         * @param expected 期望的当前值
         * @param new_value 要设置的新值
         * @param ttl 过期时间（秒），0 表示不设置过期时间
         * @return Result<bool> 成功返回是否交换成功，失败返回错误
         */
        [[nodiscard]]
        auto CompareAndSwap(
            const std::string& key,
            const std::string& expected,
            const std::string& new_value,
            int ttl,
            disk::utils::LogContext log_context = {}
        ) -> drogon::Task<Result<bool>>;

        /**
         * @brief 原子性递增计数并设置过期时间（用于频率限制）
         *
         * 使用 Lua 脚本实现原子 INCR + 条件 EXPIRE 操作：
         * 1. 递增键值
         * 2. 如果新值为 1，则设置过期时间
         * 3. 返回递增后的值
         *
         * @param key Redis 键
         * @param ttl 过期时间（秒）
         * @return Result<std::int64_t> 成功返回递增后的新值，失败返回错误
         */
        [[nodiscard]]
        auto IncrWithExpire(
            const std::string& key,
            int ttl,
            disk::utils::LogContext log_context = {}
        ) -> drogon::Task<Result<std::int64_t>>;

    private:
        /**
         * @brief 私有构造函数（单例模式）
         */
        RedisService() = default;

        drogon::nosql::RedisClientPtr m_redis_client;
    };

} // namespace disk::services
