/**
 * @file TokenService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 统一JWT令牌服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "TokenService.hpp"

#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/traits.h>

#include "utils/ErrorCode.hpp"
#include "utils/HashUtil.hpp"
#include "utils/RedisKeyPrefix.hpp"

namespace disk::services {

    using disk::error::ErrorInfo;

    static constexpr int REFRESH_TOKEN_TTL = 604800;
    static constexpr int ACCESS_TOKEN_TTL = 7200;
    static constexpr int SHARE_TOKEN_TTL = 3600;

    // TokenService 私有构造函数（单例模式）
    TokenService::TokenService()
        : m_jwt_secret(),
          m_redis_client(nullptr),
          m_redis_service(RedisService::GetInstance()) {
        LOG_DEBUG << "TokenService initialization completed";
    }

    void TokenService::Initialize(std::string jwt_secret) {
        GetInstance()->m_jwt_secret = std::move(jwt_secret);
    }

    auto TokenService::GenerateTokens(uint64_t user_id, const std::string& username) const
        -> std::pair<std::string, std::string> {

        const auto now = std::chrono::system_clock::now();

        // 定义 traits 别名
        using traits = jwt::traits::open_source_parsers_jsoncpp;

        // 生成 JTI (Access Token 和 Refresh Token 各有独立的 JTI)
        const auto access_jti = drogon::utils::getUuid();
        const auto refresh_jti = drogon::utils::getUuid();

        // 生成 Access Token (带唯一 JTI)
        jwt::builder<jwt::default_clock, traits> access_builder{ jwt::default_clock{} };
        auto access_token =
            access_builder.set_issuer("disk")
                .set_type("JWT")
                .set_subject(std::to_string(user_id))
                .set_payload_claim("username", username)
                .set_payload_claim("type", "access")
                .set_payload_claim("jti", access_jti)
                .set_issued_at(now)
                .set_expires_at(now + std::chrono::seconds(GetAccessTokenExpireSeconds()))
                .sign(jwt::algorithm::hs256{ m_jwt_secret });

        // 生成 Refresh Token (带唯一 JTI)
        jwt::builder<jwt::default_clock, traits> refresh_builder{ jwt::default_clock{} };
        auto refresh_token =
            refresh_builder.set_issuer("disk")
                .set_type("JWT")
                .set_subject(std::to_string(user_id))
                .set_payload_claim("type", "refresh")
                .set_payload_claim("jti", refresh_jti)
                .set_issued_at(now)
                .set_expires_at(now + std::chrono::seconds(GetRefreshTokenExpireSeconds()))
                .sign(jwt::algorithm::hs256{ m_jwt_secret });
        LOG_DEBUG << "Generating token pair: user_id=" << user_id << ", access_jti=" << access_jti
                  << ", refresh_jti=" << refresh_jti;
        return { access_token, refresh_token };
    }

    auto TokenService::VerifyAccessToken(const std::string& token) const
        -> Result<AccessTokenClaims> {

        using traits = jwt::traits::open_source_parsers_jsoncpp;

        try {
            auto decoded = jwt::decode<traits>(token);

            auto verifier = jwt::verify<traits>()
                                .allow_algorithm(jwt::algorithm::hs256{ m_jwt_secret })
                                .with_issuer("disk");

            verifier.verify(decoded);

            const auto type = decoded.get_payload_claim("type").as_string();
            if (type != "access") {
                return std::unexpected(ErrorInfo(disk::error::Code::TokenWrongType));
            }

            const auto user_id_str = decoded.get_subject();
            const auto username = decoded.get_payload_claim("username").as_string();
            const auto jti = decoded.get_payload_claim("jti").as_string();

            const auto user_id = std::stoull(user_id_str);

            LOG_TRACE << "JWT verification successful: user_id=" << user_id
                      << ", username=" << username << ", jti=" << jti;
            return AccessTokenClaims{ .user_id = user_id, .username = username, .jti = jti };

        } catch (const jwt::error::token_verification_exception& e) {
            LOG_WARN << "JWT verification failed: " << e.what();
            if (std::string(e.what()).find("expired") != std::string::npos) {
                return std::unexpected(ErrorInfo(disk::error::Code::TokenExpired));
            }
            return std::unexpected(ErrorInfo(disk::error::Code::InvalidToken));
        } catch (const std::exception& e) {
            LOG_WARN << "JWT parsing failed: " << e.what();
            return std::unexpected(ErrorInfo(disk::error::Code::TokenMalformed));
        }
    }

    auto TokenService::VerifyRefreshToken(const std::string& token) const
        -> Result<std::pair<uint64_t, std::string>> {

        using traits = jwt::traits::open_source_parsers_jsoncpp;

        try {
            auto decoded = jwt::decode<traits>(token);

            auto verifier = jwt::verify<traits>()
                                .allow_algorithm(jwt::algorithm::hs256{ m_jwt_secret })
                                .with_issuer("disk");

            verifier.verify(decoded);

            const auto type = decoded.get_payload_claim("type").as_string();
            if (type != "refresh") {
                return std::unexpected(ErrorInfo(disk::error::Code::TokenWrongType));
            }

            const auto jti = decoded.get_payload_claim("jti").as_string();

            const auto user_id_str = decoded.get_subject();
            const auto user_id = std::stoull(user_id_str);

            LOG_TRACE << "Refresh token verification successful: user_id=" << user_id
                      << ", jti=" << jti;
            return std::make_pair(user_id, jti);

        } catch (const jwt::error::token_verification_exception& e) {
            LOG_WARN << "Refresh token verification failed: " << e.what();
            if (std::string(e.what()).find("expired") != std::string::npos) {
                return std::unexpected(ErrorInfo(disk::error::Code::TokenExpired));
            }
            return std::unexpected(ErrorInfo(disk::error::Code::InvalidRefreshToken));
        } catch (const std::exception& e) {
            LOG_WARN << "Refresh token parsing failed: " << e.what();
            return std::unexpected(ErrorInfo(disk::error::Code::TokenMalformed));
        }
    }

    auto TokenService::StoreRefreshToken(uint64_t user_id, const std::string& refresh_token)
        -> drogon::Task<Result<void>> {
        const auto key = disk::redis::RedisKeyPrefix::BuildRefreshTokenKey(user_id);

        auto hash_result = disk::utils::HashUtil::HashToken(refresh_token);
        if (!hash_result) {
            co_return std::unexpected(hash_result.error());
        }
        const auto hash = disk::utils::HashUtil::TokenHashToHex(hash_result.value());

        co_return co_await m_redis_service->Set(key, hash, REFRESH_TOKEN_TTL);
    }

    auto TokenService::RefreshRefreshToken(
        uint64_t user_id,
        const std::string& old_token,
        const std::string& new_token
    ) -> drogon::Task<Result<void>> {
        const auto key = disk::redis::RedisKeyPrefix::BuildRefreshTokenKey(user_id);

        auto old_hash_result = disk::utils::HashUtil::HashToken(old_token);
        if (!old_hash_result) {
            co_return std::unexpected(old_hash_result.error());
        }
        auto new_hash_result = disk::utils::HashUtil::HashToken(new_token);
        if (!new_hash_result) {
            co_return std::unexpected(new_hash_result.error());
        }
        const auto old_hash = disk::utils::HashUtil::TokenHashToHex(old_hash_result.value());
        const auto new_hash = disk::utils::HashUtil::TokenHashToHex(new_hash_result.value());

        // Atomic CAS: compare and swap in one operation
        // Uses Lua script to ensure exactly one success under concurrency
        auto cas_result = co_await m_redis_service->CompareAndSwap(
            key,
            old_hash,
            new_hash,
            REFRESH_TOKEN_TTL
        );

        if (!cas_result) {
            LOG_ERROR << "Redis CAS operation failed: user_id=" << user_id;
            co_return std::unexpected(cas_result.error());
        }

        if (!cas_result.value()) {
            LOG_WARN << "Refresh token already used or refreshed (CAS failed): user_id=" << user_id;
            co_return std::unexpected(ErrorInfo(disk::error::Code::RefreshTokenAlreadyUsed));
        }

        LOG_DEBUG << "Refresh token rotated successfully: user_id=" << user_id;
        co_return {};
    }

    auto TokenService::InvalidateAccessToken(const std::string& token)
        -> drogon::Task<Result<void>> {
        auto jti_result = ExtractJti(token);
        if (!jti_result) {
            co_return std::unexpected(jti_result.error());
        }

        const auto jti = jti_result.value();

        // 无论 Redis 结果如何，立即覆盖本地缓存为 revoked=true
        {
            const auto now = std::chrono::steady_clock::now();
            std::lock_guard<std::mutex> lock(m_cache_mutex);
            m_revocation_cache[jti] = RevocationCacheEntry{
                .is_revoked = true,
                .expires_at = now + std::chrono::seconds(GetAccessTokenExpireSeconds())
            };
        }

        const auto key = disk::redis::RedisKeyPrefix::BuildAccessTokenBlacklistKey(jti);
        co_return co_await m_redis_service->Set(key, "1", ACCESS_TOKEN_TTL);
    }

    auto TokenService::RevokeRefreshToken(uint64_t user_id) -> drogon::Task<Result<void>> {
        const auto key = disk::redis::RedisKeyPrefix::BuildRefreshTokenKey(user_id);
        co_return co_await m_redis_service->Delete(key);
    }

    auto TokenService::IsAccessTokenRevoked(const std::string& jti) -> drogon::Task<bool> {
        const auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(m_cache_mutex);
            auto it = m_revocation_cache.find(jti);
            if (it != m_revocation_cache.end()) {
                if (it->second.expires_at > now) {
                    co_return it->second.is_revoked;
                }
                // 仅擦除当前过期的单条目 — 不遍历整个缓存
                m_revocation_cache.erase(it);
            }
        }

        const auto key = disk::redis::RedisKeyPrefix::BuildAccessTokenBlacklistKey(jti);
        const auto revoked = co_await m_redis_service->Exists(key);

        {
            std::lock_guard<std::mutex> lock(m_cache_mutex);
            m_revocation_cache[jti] = RevocationCacheEntry{
                .is_revoked = revoked,
                .expires_at = now + std::chrono::seconds(
                                        revoked ? GetAccessTokenExpireSeconds() : GetNegativeCacheTtlSeconds()
                                    )
            };
        }

        co_return revoked;
    }

    auto TokenService::ClearRevocationCache() -> void {
        std::lock_guard<std::mutex> lock(m_cache_mutex);
        m_revocation_cache.clear();
    }

    auto TokenService::SetRevocationCacheEntryForTest(
        const std::string& jti,
        bool is_revoked,
        int ttl_seconds
    ) -> void {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(m_cache_mutex);
        m_revocation_cache[jti] = RevocationCacheEntry{
            .is_revoked = is_revoked,
            .expires_at = now + std::chrono::seconds(ttl_seconds)
        };
    }

    auto TokenService::GetRevocationCacheSizeForTest() const -> size_t {
        std::lock_guard<std::mutex> lock(m_cache_mutex);
        return m_revocation_cache.size();
    }

    auto TokenService::StartCacheMaintenance() -> void {
        drogon::app().getLoop()->runEvery(
            CACHE_MAINTENANCE_INTERVAL_SECONDS,
            [this]() { EvictExpiredCacheEntries(); }
        );
        LOG_DEBUG << "Revocation cache maintenance timer started (interval="
                  << CACHE_MAINTENANCE_INTERVAL_SECONDS << "s)";
    }

    auto TokenService::EvictExpiredCacheEntries() const -> void {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(m_cache_mutex);
        for (auto it = m_revocation_cache.begin(); it != m_revocation_cache.end();) {
            if (it->second.expires_at <= now) {
                it = m_revocation_cache.erase(it);
            } else {
                ++it;
            }
        }
    }

    auto TokenService::ExtractJti(const std::string& token) const -> Result<std::string> {
        using traits = jwt::traits::open_source_parsers_jsoncpp;

        try {
            auto decoded = jwt::decode<traits>(token);

            // 检查 JTI claim 是否存在（access 和 refresh token 现在都有 JTI）
            if (decoded.has_payload_claim("jti")) {
                const auto jti = decoded.get_payload_claim("jti");
                return jti.as_string();
            }

            LOG_WARN << "Token missing JTI claim";
            return std::unexpected(ErrorInfo(disk::error::Code::TokenMalformed));
        } catch (const jwt::error::token_verification_exception& e) {
            LOG_WARN << "Failed to extract JTI: " << e.what();
            return std::unexpected(ErrorInfo(disk::error::Code::TokenMalformed));
        } catch (const std::exception& e) {
            LOG_WARN << "Failed to extract JTI: " << e.what();
            return std::unexpected(ErrorInfo(disk::error::Code::TokenMalformed));
        }
    }

    auto TokenService::CalculateRemainingTtl(const std::string& token) const -> Result<int> {
        using traits = jwt::traits::open_source_parsers_jsoncpp;

        try {
            auto decoded = jwt::decode<traits>(token);
            auto exp = decoded.get_expires_at();
            auto now = std::chrono::system_clock::now();

            auto remaining = std::chrono::duration_cast<std::chrono::seconds>(exp - now).count();

            if (remaining <= 0) {
                return std::unexpected(ErrorInfo(disk::error::Code::TokenExpired));
            }

            return static_cast<int>(remaining);
        } catch (const std::exception& e) {
            LOG_WARN << "Failed to calculate TTL: " << e.what();
            return std::unexpected(ErrorInfo(disk::error::Code::TokenMalformed));
        }
    }

    // ==================== Share Token 静态方法 ====================

    auto TokenService::GenerateShareToken(
        const std::string& jwt_secret,
        const std::string& share_code,
        uint64_t share_id
    ) -> Result<std::string> {
        using traits = jwt::traits::open_source_parsers_jsoncpp;

        const auto now = std::chrono::system_clock::now();
        const auto jti = drogon::utils::getUuid();

        try {
            jwt::builder<jwt::default_clock, traits> builder{ jwt::default_clock{} };
            auto token =
                builder.set_issuer("disk_share")
                    .set_type("JWT")
                    .set_subject(std::to_string(share_id))
                    .set_payload_claim("share_code", share_code)
                    .set_payload_claim("type", "share")
                    .set_payload_claim("jti", jti)
                    .set_issued_at(now)
                    .set_expires_at(now + std::chrono::seconds(GetShareTokenExpireSeconds()))
                    .sign(jwt::algorithm::hs256{ jwt_secret });

            LOG_DEBUG << "Generated share token: share_code=" << share_code
                      << ", share_id=" << share_id;
            return token;
        } catch (const std::exception& e) {
            LOG_ERROR << "Failed to generate share token: " << e.what();
            return std::unexpected(
                ErrorInfo(disk::error::Code::InternalError, "Token generation failed")
            );
        }
    }

    auto TokenService::VerifyShareToken(const std::string& jwt_secret, const std::string& token)
        -> Result<ShareTokenClaims> {
        using traits = jwt::traits::open_source_parsers_jsoncpp;

        if (token.empty()) {
            return std::unexpected(ErrorInfo(disk::error::Code::TokenMalformed, "Token is empty"));
        }

        try {
            auto decoded = jwt::decode<traits>(token);

            auto verifier = jwt::verify<traits>()
                                .allow_algorithm(jwt::algorithm::hs256{ jwt_secret })
                                .with_issuer("disk_share");

            verifier.verify(decoded);

            const auto type = decoded.get_payload_claim("type").as_string();
            if (type != "share") {
                return std::unexpected(ErrorInfo(disk::error::Code::TokenWrongType));
            }

            const auto share_code = decoded.get_payload_claim("share_code").as_string();
            const auto jti = decoded.get_payload_claim("jti").as_string();
            const auto share_id_str = decoded.get_subject();
            const auto share_id = std::stoull(share_id_str);

            LOG_DEBUG << "Share token verification successful: share_code=" << share_code
                      << ", share_id=" << share_id;
            return ShareTokenClaims{ .share_code = share_code, .share_id = share_id, .jti = jti };

        } catch (const jwt::error::token_verification_exception& e) {
            LOG_WARN << "Share token verification failed: " << e.what();
            if (std::string(e.what()).find("expired") != std::string::npos) {
                return std::unexpected(ErrorInfo(disk::error::Code::TokenExpired));
            }
            return std::unexpected(ErrorInfo(disk::error::Code::TokenMalformed));
        } catch (const std::exception& e) {
            LOG_WARN << "Share token parsing failed: " << e.what();
            return std::unexpected(ErrorInfo(disk::error::Code::TokenMalformed));
        }
    }

    auto TokenService::ExtractShareTokenHash(const std::string& token) -> Result<std::string> {
        auto hash_result = disk::utils::HashUtil::HashToken(token);
        if (!hash_result) {
            return std::unexpected(hash_result.error());
        }
        return disk::utils::HashUtil::TokenHashToHex(hash_result.value());
    }

    // ==================== Share Token Redis 异步方法 ====================

    auto TokenService::VerifyShareTokenWithRedis(const std::string& share_code, const std::string& token)
        -> drogon::Task<Result<ShareTokenClaims>> {
        auto verify_result = VerifyShareToken(m_jwt_secret, token);
        if (!verify_result) {
            co_return std::unexpected(verify_result.error());
        }

        auto hash_result = ExtractShareTokenHash(token);
        if (!hash_result) {
            co_return std::unexpected(hash_result.error());
        }

        if (co_await IsShareTokenRevoked(hash_result.value())) {
            LOG_WARN << "Share token has been revoked: share_code=" << share_code;
            co_return std::unexpected(ErrorInfo(disk::error::Code::TokenRevoked));
        }

        co_return verify_result.value();
    }

    auto TokenService::RevokeShareToken(const std::string& token) -> drogon::Task<Result<void>> {
        auto hash_result = ExtractShareTokenHash(token);
        if (!hash_result) {
            co_return std::unexpected(hash_result.error());
        }

        const auto key =
            disk::redis::RedisKeyPrefix::BuildShareTokenBlacklistKey(hash_result.value());
        co_return co_await m_redis_service->Set(key, "1", SHARE_TOKEN_TTL);
    }

    auto TokenService::IsShareTokenRevoked(const std::string& token_hash) -> drogon::Task<bool> {
        const auto key = disk::redis::RedisKeyPrefix::BuildShareTokenBlacklistKey(token_hash);
        co_return co_await m_redis_service->Exists(key);
    }

} // namespace disk::services
