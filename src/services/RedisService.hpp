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

#include <drogon/nosql/RedisClient.h>

#include "utils/ErrorCode.hpp"

namespace disk::services {

    /**
     * @brief Redis 服务类
     *
     * 封装 Redis 客户端操作，提供 refresh_token 存储和验证接口
     */
    class RedisService {
    public:
        explicit RedisService(drogon::nosql::RedisClientPtr redis_client);
        ~RedisService() = default;
        RedisService(const RedisService&) = delete;
        auto operator=(const RedisService&) -> RedisService& = delete;
        RedisService(RedisService&&) = default;
        auto operator=(RedisService&&) -> RedisService& = default;

        /**
         * @brief 存储 refresh_token 到 Redis
         *
         * @param user_id 用户 ID
         * @param refresh_token 刷新令牌
         * @return drogon::Task<Result<void>> 成功返回 void，失败返回错误
         */
        [[nodiscard]]
        auto StoreRefreshToken(uint64_t user_id, const std::string& refresh_token)
            -> drogon::Task<Result<void>>;

        /**
         * @brief 刷新 refresh_token（验证旧 token 并存储新 token）
         *
         * @param user_id 用户 ID
         * @param old_token 旧的刷新令牌
         * @param new_token 新的刷新令牌
         * @return drogon::Task<Result<void>> 成功返回 void，失败返回错误
         */
        [[nodiscard]]
        auto RefreshRefreshToken(
            uint64_t user_id,
            const std::string& old_token,
            const std::string& new_token
        ) -> drogon::Task<Result<void>>;

        /**
         * @brief 使访问令牌失效（加入黑名单）
         *
         * @param token 访问令牌
         * @return drogon::Task<Result<void>> 成功返回 void，失败返回错误
         */
        [[nodiscard]]
        auto InvalidateAccessToken(const std::string& token) -> drogon::Task<Result<void>>;

        /**
         * @brief 撤销刷新令牌
         *
         * @param user_id 用户 ID
         * @return drogon::Task<Result<void>> 成功返回 void，失败返回错误
         */
        [[nodiscard]]
        auto RevokeRefreshToken(uint64_t user_id) -> drogon::Task<Result<void>>;

        /**
         * @brief 检查访问令牌是否被撤销
         *
         * @param jti 令牌 JTI
         * @return drogon::Task<bool> true 表示被撤销
         */
        [[nodiscard]]
        auto IsAccessTokenRevoked(const std::string& jti) -> drogon::Task<bool>;

    private:
        drogon::nosql::RedisClientPtr m_redis_client;
        static constexpr int ACCESS_TOKEN_TTL = 7200;    // 2小时
        static constexpr int REFRESH_TOKEN_TTL = 604800; // 7天
    };

} // namespace disk::services
