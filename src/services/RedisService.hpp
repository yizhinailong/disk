/**
 * @file RedisService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Redis 服务封装
 * @version 0.1
 * @date 2026-01-21
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <string>
#include <vector>

#include <drogon/nosql/RedisClient.h>

#include "utils/ErrorCode.hpp"

namespace disk::services {

    // ==================== 辅助类型 ====================

    /**
     * @brief TTL类型枚举
     */
    enum class TtlType : int {
        Permanent = 0, ///< 永不过期
        Auto           ///< 自动过期（使用参数值）
    };

    /**
     * @brief 键值对结构（用于批操作）
     */
    struct KeyValue {
        std::string key;   ///< Redis键
        std::string value; ///< Redis值
    };

    /**
     * @brief Redis 服务类
     *
     * 封装 Redis 客户端操作，提供通用 Redis 接口
     */
    class RedisService {
    public:
        explicit RedisService(drogon::nosql::RedisClientPtr redis_client);
        ~RedisService() = default;
        RedisService(const RedisService&) = delete;
        auto operator=(const RedisService&) -> RedisService& = delete;
        RedisService(RedisService&&) = default;
        auto operator=(RedisService&&) -> RedisService& = default;

        // ==================== 通用方法 ====================

        /**
         * @brief 设置 Redis 键值对
         * @param key Redis 键
         * @param value Redis 值
         * @param ttl 过期时间（秒），0 表示不过期
         * @return Result<void> 成功返回 void，失败返回错误
         */
        [[nodiscard]]
        auto Set(const std::string& key, const std::string& value, int ttl = 0)
            -> drogon::Task<Result<void>>;

        /**
         * @brief 获取 Redis 键对应的值
         * @param key Redis 键
         * @return Result<std::string> 成功返回值，失败返回错误
         */
        [[nodiscard]]
        auto Get(const std::string& key) -> drogon::Task<Result<std::string>>;

        /**
         * @brief 删除 Redis 键
         * @param key Redis 键
         * @return Result<void> 成功返回 void，失败返回错误
         */
        [[nodiscard]]
        auto Delete(const std::string& key) -> drogon::Task<Result<void>>;

        /**
         * @brief 检查 Redis 键是否存在
         * @param key Redis 键
         * @return bool true 表示存在，false 表示不存在
         */
        [[nodiscard]]
        auto Exists(const std::string& key) -> drogon::Task<bool>;

        /**
         * @brief 设置 Redis 键的过期时间
         * @param key Redis 键
         * @param ttl 过期时间（秒）
         * @return Result<void> 成功返回 void，失败返回错误
         */
        [[nodiscard]]
        auto Expire(const std::string& key, int ttl) -> drogon::Task<Result<void>>;

        /**
         * @brief 批量设置键值对
         *
         * @param pairs 键值对数组
         * @param ttl 过期时间（0=永久，>0=自动过期）
         * @return Result<void> 成功返回void，失败返回错误
         */
        [[nodiscard]]
        auto MSet(const std::vector<KeyValue>& pairs, int ttl) -> drogon::Task<Result<void>>;

        /**
         * @brief 批量获取键值
         *
         * @param keys 键数组
         * @return Result<std::vector<std::string>> 成功返回值数组，失败返回错误
         */
        [[nodiscard]]
        auto MGet(const std::vector<std::string>& keys) -> drogon::Task<Result<std::vector<std::string>>>;

        /**
         * @brief 批量删除键
         *
         * @param keys 键数组
         * @return Result<int> 成功返回删除的键数量，失败返回错误
         */
        [[nodiscard]]
        auto MDelete(const std::vector<std::string>& keys) -> drogon::Task<Result<int>>;

    private:
        drogon::nosql::RedisClientPtr m_redis_client;
    };

} // namespace disk::services
