/**
 * @file TokenService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 统一JWT令牌服务（Access、Refresh、Share Token）
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include <drogon/nosql/RedisClient.h>
#include <gtest/gtest_prod.h>
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/traits.h>

#include "services/RedisService.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/Singleton.hpp"

namespace trantor {
    class EventLoop;
}

namespace disk {
    namespace services {
        namespace detail {

            [[nodiscard]]
            auto GetAuthCpuWorkLoop() -> trantor::EventLoop*;

        } // namespace detail
    } // namespace services
} // namespace disk

namespace disk::services {

    /**
     * @brief 访问令牌声明信息
     */
    struct AccessTokenClaims {
        uint64_t user_id;
        std::string username;
        std::string jti;
        int role{0};
    };

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
     *
     * 单例模式：使用 TokenService::Initialize() 初始化，TokenService::GetInstance() 获取实例。
     */
    class TokenService : public utils::Singleton<TokenService> {
        friend class utils::Singleton<TokenService>;

    public:
        /**
         * @brief 初始化单例
         * @param jwt_secret JWT签名密钥
         * @note 必须在使用 GetInstance() 之前调用一次
         */
        static void Initialize(std::string jwt_secret);

        // 禁用拷贝和移动操作
        TokenService(const TokenService&) = delete;
        TokenService& operator=(const TokenService&) = delete;
        TokenService(TokenService&&) = delete;
        TokenService& operator=(TokenService&&) = delete;

        // ==================== Access/Refresh Token 实例方法 ====================

        /**
         * @brief 生成令牌对
         * @param user_id 用户ID
         * @param username 用户名
         * @return pair<access_token, refresh_token>
         */
        [[nodiscard]]
        auto GenerateTokens(uint64_t user_id, const std::string& username, int role = 0) const -> std::pair<std::string, std::string>;

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
         * @brief 获取否定缓存条目最大存活时间（秒）
         *
         * 未撤销令牌（revoked=false）的本地缓存条目最多存活 5 秒，
         * 确保撤销操作能在短时间内生效。
         *
         * @return int 最大存活时间（秒）
         */
        [[nodiscard]]
        static constexpr auto GetNegativeCacheTtlSeconds() noexcept -> int {
            return 5;
        }

        /**
         * @brief 启动撤销缓存后台清理任务
         *
         * 在 Drogon 事件循环中注册周期性定时器，定期清理过期缓存条目。
         * 应在应用启动后（beginning advice）调用一次。
         */
        auto StartCacheMaintenance() -> void;

        /**
         * @brief 验证访问令牌
         * @param token 访问令牌字符串
         * @return Result<AccessTokenClaims> 验证成功返回声明信息（含jti），失败返回错误
         */
        [[nodiscard]]
        auto VerifyAccessToken(const std::string& token) const -> Result<AccessTokenClaims>;

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

        auto ClearRevocationCache() -> void;

        // ==================== 测试辅助方法 ====================

        /**
         * @brief 测试用：直接向本地撤销缓存插入条目
         *
         * 仅用于单元测试，绕过 Redis 直接设置本地缓存状态。
         * 正常运行时不调用此方法。
         *
         * @param jti 令牌 JTI
         * @param is_revoked 是否已撤销
         * @param ttl_seconds 缓存条目存活时间（秒）
         */
        auto SetRevocationCacheEntryForTest(
            const std::string& jti,
            bool is_revoked,
            int ttl_seconds
        ) -> void;

        /**
         * @brief 测试用：获取本地撤销缓存条目数
         * @return size_t 缓存条目数量
         */
        [[nodiscard]]
        auto GetRevocationCacheSizeForTest() const -> size_t;

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
         * 优先检查本地否定缓存（5秒 TTL），缓存未命中时回退到 Redis。
         *
         * @param token_hash 令牌哈希
         * @return drogon::Task<bool> true 表示已撤销
         */
        [[nodiscard]]
        auto IsShareTokenRevoked(const std::string& token_hash) -> drogon::Task<bool>;

        /**
         * @brief 测试用：直接向分享令牌撤销缓存插入条目
         *
         * 仅用于单元测试，绕过 Redis 直接设置本地缓存状态。
         *
         * @param token_hash 令牌哈希
         * @param is_revoked 是否已撤销
         * @param ttl_seconds 缓存条目存活时间（秒）
         */
        auto SetShareRevocationCacheEntryForTest(
            const std::string& token_hash,
            bool is_revoked,
            int ttl_seconds
        ) -> void;

        /**
         * @brief 测试用：获取分享令牌撤销缓存条目数
         * @return size_t 缓存条目数量
         */
        [[nodiscard]]
        auto GetShareRevocationCacheSizeForTest() const -> size_t;

        /**
         * @brief 测试用：清空分享令牌撤销缓存
         */
        auto ClearShareRevocationCache() -> void;

    private:
        using JwtTraits = jwt::traits::open_source_parsers_jsoncpp;
        using JwtVerifier = jwt::verifier<jwt::default_clock, JwtTraits>;

        /**
         * @brief 私有构造函数（单例模式）
         */
        TokenService();

        [[nodiscard]]
        static auto BuildJwtVerifier(const std::string& jwt_secret) -> JwtVerifier;

        [[nodiscard]]
        static auto BuildShareJwtVerifier(const std::string& jwt_secret) -> JwtVerifier;

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

        static constexpr int CACHE_MAINTENANCE_INTERVAL_SECONDS = 60;

        /// 访问令牌撤销缓存条目
        struct RevocationCacheEntry {
            bool is_revoked;
            std::chrono::steady_clock::time_point expires_at;
        };

        /// 分享令牌撤销缓存条目
        struct ShareCacheEntry {
            bool is_revoked;
            std::chrono::steady_clock::time_point expires_at;
        };

        auto EvictExpiredCacheEntries() const -> void;

        std::string m_jwt_secret;
        JwtVerifier m_jwt_verifier;
        JwtVerifier m_share_jwt_verifier;
        drogon::nosql::RedisClientPtr m_redis_client;
        std::shared_ptr<RedisService> m_redis_service;
        mutable std::shared_mutex m_cache_mutex;
        mutable std::unordered_map<std::string, RevocationCacheEntry> m_revocation_cache;
        mutable std::shared_mutex m_share_cache_mutex;
        mutable std::unordered_map<std::string, ShareCacheEntry> m_share_revocation_cache;
    };

} // namespace disk::services
