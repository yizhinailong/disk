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

#include "services/RedisService.hpp"
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
         * @param redis_service Redis服务
         */
        explicit TokenService(std::string jwt_secret, disk::services::RedisService& redis_service);

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
         * @return drogon::Task<Result<void>> 成功返回 void，失败返回错误
         */
        [[nodiscard]]
        auto StoreRefreshToken(uint64_t user_id, const std::string& refresh_token)
            -> drogon::Task<Result<void>>;

        /**
         * @brief 刷新 refresh_token
         *
         * @param user_id 用户 ID
         * @param old_token 旧的刷新令牌
         * @param new_token 新的刷新令牌
         * @return drogon::Task<Result<void>> 成功返回 void，失败返回错误
         */
        [[nodiscard]]
        auto RefreshRefreshToken(uint64_t user_id, const std::string& old_token, const std::string& new_token)
            -> drogon::Task<Result<void>>;

        /**
         * @brief 使访问令牌失效（加入黑名单）
         * @param token 访问令牌
         * @return drogon::Task<Result<void>> 成功返回 void，失败返回错误
         */
        [[nodiscard]]
        auto InvalidateAccessToken(const std::string& token) -> drogon::Task<Result<void>>;

        /**
         * @brief 撤销刷新令牌
         * @param user_id 用户 ID
         * @return drogon::Task<Result<void>> 成功返回 void，失败返回错误
         */
        [[nodiscard]]
        auto RevokeRefreshToken(uint64_t user_id) -> drogon::Task<Result<void>>;

        /**
         * @brief 检查访问令牌是否被撤销
         * @param jti 令牌 JTI
         * @return drogon::Task<bool> true 表示被撤销
         */
        [[nodiscard]]
        auto IsAccessTokenRevoked(const std::string& jti) -> drogon::Task<bool>;

    private:
        /**
         * @brief 从 JWT 中提取 JTI
         * @param token JWT 令牌
         * @return Result<std::string> 成功返回 JTI，失败返回错误
         */
        [[nodiscard]]
        auto ExtractJti(const std::string& token) const -> Result<std::string>;

        /**
         * @brief 计算令牌剩余 TTL（秒）
         * @param token JWT 令牌
         * @return Result<int> 成功返回剩余秒数，失败返回错误
         */
        [[nodiscard]]
        auto CalculateRemainingTtl(const std::string& token) const -> Result<int>;

        std::string m_jwt_secret;
        disk::services::RedisService& m_redis_service;
    };

} // namespace disk::auth
