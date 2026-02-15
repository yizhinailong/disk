/**
 * @file TokenService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 统一JWT令牌服务（Access、Refresh、Share Token）
 * @version 0.1
 * @date 2026-01-16
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include <drogon/nosql/RedisClient.h>

#include "services/RedisService.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::services {

    /**
     * @brief 分享令牌声明信息
     */
    struct ShareTokenClaims {
        std::string share_code;
        uint64_t share_id;
        std::string jti;
    };

    /**
     * @brief 统一JWT令牌服务
     *
     * 提供 Access Token、Refresh Token 和 Share Token 的生成与验证。
     */
    class TokenService {
    public:
        /**
         * @brief 构造函数（内部创建RedisService）
         * @param jwt_secret JWT签名密钥
         * @param redis_client Redis客户端
         */
        explicit TokenService(std::string jwt_secret, drogon::nosql::RedisClientPtr redis_client);

        /**
         * @brief 构造函数（外部提供RedisService）
         * @param jwt_secret JWT签名密钥
         * @param redis_service 已创建的RedisService
         */
        explicit TokenService(std::string jwt_secret, std::shared_ptr<RedisService> redis_service);

        // ==================== Access/Refresh Token 实例方法 ====================

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

        // ==================== Share Token 静态方法 ====================

        /**
         * @brief 生成分享令牌（静态方法）
         *
         * 契约：
         * - issuer = "disk_share"
         * - type = "share"
         * - claims: share_code, share_id (subject), jti
         * - TTL = 3600 秒
         *
         * @param jwt_secret JWT签名密钥
         * @param share_code 分享码
         * @param share_id 分享ID
         * @return Result<std::string> 成功返回令牌，失败返回错误
         */
        [[nodiscard]]
        static auto GenerateShareToken(
            const std::string& jwt_secret,
            const std::string& share_code,
            uint64_t share_id
        ) -> Result<std::string>;

        /**
         * @brief 验证分享令牌（静态方法）
         *
         * @param jwt_secret JWT签名密钥
         * @param token 分享令牌
         * @return Result<ShareTokenClaims> 成功返回声明信息，失败返回错误
         */
        [[nodiscard]]
        static auto VerifyShareToken(
            const std::string& jwt_secret,
            const std::string& token
        ) -> Result<ShareTokenClaims>;

        /**
         * @brief 提取分享令牌哈希（静态方法）
         *
         * @param token 分享令牌
         * @return Result<std::string> 成功返回哈希字符串（64位十六进制），失败返回错误
         */
        [[nodiscard]]
        static auto ExtractShareTokenHash(const std::string& token) -> Result<std::string>;

        /**
         * @brief 获取分享令牌过期时间（秒）
         * @return int 过期时间（秒），默认3600秒（1小时）
         */
        [[nodiscard]]
        static constexpr auto GetShareTokenExpireSeconds() noexcept -> int {
            return 3600;
        }

        // ==================== Share Token Redis 异步方法 ====================

        /**
         * @brief 存储分享令牌到 Redis
         *
         * @param share_code 分享码
         * @param token 分享令牌
         * @return drogon::Task<Result<void>> 成功返回 void，失败返回错误
         */
        [[nodiscard]]
        auto StoreShareToken(const std::string& share_code, const std::string& token)
            -> drogon::Task<Result<void>>;

        /**
         * @brief 验证分享令牌（含 Redis 撤销检查）
         *
         * @param share_code 分享码
         * @param token 分享令牌
         * @return drogon::Task<Result<ShareTokenClaims>> 成功返回声明信息，失败返回错误
         */
        [[nodiscard]]
        auto VerifyShareTokenWithRedis(const std::string& share_code, const std::string& token)
            -> drogon::Task<Result<ShareTokenClaims>>;

        /**
         * @brief 撤销分享令牌
         *
         * @param token 分享令牌
         * @return drogon::Task<Result<void>> 成功返回 void，失败返回错误
         */
        [[nodiscard]]
        auto RevokeShareToken(const std::string& token) -> drogon::Task<Result<void>>;

        /**
         * @brief 检查分享令牌是否已被撤销
         *
         * @param token_hash 令牌哈希
         * @return drogon::Task<bool> true 表示已撤销
         */
        [[nodiscard]]
        auto IsShareTokenRevoked(const std::string& token_hash) -> drogon::Task<bool>;

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
        drogon::nosql::RedisClientPtr m_redis_client;
        std::shared_ptr<RedisService> m_redis_service;
    };

} // namespace disk::services
