/**
 * @file TokenService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief JWT令牌服务（最小化实现）
 * @version 0.1
 * @date 2026-01-16
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <string>
#include <utility>

#include <drogon/nosql/RedisClient.h>

#include "utils/ErrorCode.hpp"

namespace disk::auth {

    /**
     * @brief JWT令牌服务
     *
     * 最小化实现：
     * - 仅支持生成 Access Token 和 Refresh Token
     * - 不实现验证、黑名单等功能（后续需要时添加）
     */
    class TokenService {
    public:
        /**
         * @brief 构造函数
         * @param jwt_secret JWT签名密钥
         * @param redis_client Redis客户端
         */
        explicit TokenService(std::string jwt_secret, drogon::nosql::RedisClientPtr redis_client);

        /**
         * @brief 生成令牌对
         * @param user_id 用户ID
         * @param username 用户名
         * @return pair<access_token, refresh_token>
         */
        [[nodiscard]]
        auto GenerateTokens(uint64_t user_id, const std::string& username) const -> std::pair<std::string, std::string>;

        /**
         * @brief 获取访问令牌过期时间（秒）
         * @return int 过期时间（秒）
         */
        [[nodiscard]]
        static constexpr auto GetAccessTokenExpireSeconds() noexcept -> int {
            return 7200;
        }

        /**
         * @brief 获取刷新令牌过期时间（秒）
         * @return int 过期时间（秒）
         */
        [[nodiscard]]
        static constexpr auto GetRefreshTokenExpireSeconds() noexcept -> int {
            return 604800;
        }

        /**
         * @brief 验证访问令牌
         * @param token 访问令牌字符串
         * @return Result<pair<user_id, username>> 验证成功返回用户信息，失败返回错误
         */
        [[nodiscard]]
        auto VerifyAccessToken(const std::string& token) const -> Result<std::pair<uint64_t, std::string>>;

        /**
         * @brief 验证刷新令牌
         * @param token 刷新令牌字符串
         * @return Result<pair<user_id, jti>> 验证成功返回用户ID和JTI，失败返回错误
         */
        [[nodiscard]]
        auto VerifyRefreshToken(const std::string& token) const -> Result<std::pair<uint64_t, std::string>>;

        /**
         * @brief 存储 refresh_token 到 Redis
         *
         * @param user_id 用户 ID
         * @param refresh_token 刷新令牌
         * @return drogon::Task<bool> 成功返回 true，失败返回 false
         */
        [[nodiscard]]
        auto StoreRefreshToken(uint64_t user_id, const std::string& refresh_token) const
            -> drogon::Task<bool>;

        /**
         * @brief 刷新 refresh_token
         *
         * @param user_id 用户 ID
         * @param old_token 旧的刷新令牌
         * @param new_token 新的刷新令牌
         * @return drogon::Task<Result<void>> 成功返回 void，失败返回错误
         */
        [[nodiscard]]
        auto RefreshRefreshToken(uint64_t user_id, const std::string& old_token, const std::string& new_token) const
            -> drogon::Task<Result<void>>;

    private:
        std::string m_jwt_secret;
        drogon::nosql::RedisClientPtr m_redis_client;
    };

} // namespace disk::auth
