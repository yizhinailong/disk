/**
 * @file TokenService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 统一JWT令牌服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "TokenService.hpp"

#include <atomic>
#include <functional>

#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/traits.h>
#include <trantor/net/EventLoopThreadPool.h>

#include "utils/ConfigMgr.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/HashUtil.hpp"
#include "utils/RedisKeyPrefix.hpp"

namespace disk::services {

    namespace {

        using JwtTraits = jwt::traits::open_source_parsers_jsoncpp;
        using JwtVerifier = jwt::verifier<jwt::default_clock, JwtTraits>;

        struct PoolMetrics {
            std::atomic<size_t> active_tasks{ 0 };
            std::atomic<size_t> total_submitted{ 0 };
            std::atomic<size_t> total_completed{ 0 };
            std::atomic<size_t> peak_active{ 0 };

            auto OnSubmit() -> void {
                const auto active = active_tasks.fetch_add(1, std::memory_order_relaxed) + 1;
                total_submitted.fetch_add(1, std::memory_order_relaxed);

                size_t current_peak = peak_active.load(std::memory_order_relaxed);
                while (active > current_peak &&
                       !peak_active.compare_exchange_weak(current_peak, active, std::memory_order_relaxed)) {}
            }

            auto OnComplete() -> void {
                active_tasks.fetch_sub(1, std::memory_order_relaxed);
                total_completed.fetch_add(1, std::memory_order_relaxed);
            }
        };

        static PoolMetrics g_pool_metrics;

        [[nodiscard]] auto AuthRuntimeLogContext() -> disk::utils::LogContext {
            return { .operation = "auth_runtime" };
        }

        [[nodiscard]] auto IsValidSharePermission(const std::string& permission) -> bool {
            return permission == "view" || permission == "download";
        }

        [[nodiscard]] auto BuildJwtVerifier(const std::string& jwt_secret) -> JwtVerifier {
            return jwt::verify<JwtTraits>()
                .allow_algorithm(jwt::algorithm::hs256{ jwt_secret })
                .with_issuer("disk");
        }

        [[nodiscard]] auto BuildShareJwtVerifier(const std::string& jwt_secret) -> JwtVerifier {
            return jwt::verify<JwtTraits>()
                .allow_algorithm(jwt::algorithm::hs256{ jwt_secret })
                .with_issuer("disk_share");
        }

        [[nodiscard]] auto IsTokenExpired(
            const jwt::error::token_verification_exception& error
        ) noexcept -> bool {
            return error.code() == jwt::error::token_verification_error::token_expired;
        }

        auto CreateAuthCpuPool() -> trantor::EventLoopThreadPool* {
            const auto thread_count = disk::utils::ConfigMgr::GetInstance()->GetAuthCpuPoolThreads();
            auto* pool = new trantor::EventLoopThreadPool(
                static_cast<size_t>(thread_count),
                "AuthCpuPool"
            );
            pool->start();
            Logger::Info(AuthRuntimeLogContext()) << "Auth CPU pool initialized: threads=" << thread_count;
            return pool;
        }

    } // namespace

    using disk::error::ErrorInfo;

    static constexpr int REFRESH_TOKEN_TTL = 604800;
    static constexpr int ACCESS_TOKEN_TTL = 7200;
    static constexpr int SHARE_TOKEN_TTL = 3600;

    namespace detail {

        auto GetAuthCpuWorkLoop() -> trantor::EventLoop* {
            static auto* pool = CreateAuthCpuPool();
            return pool->getNextLoop();
        }

    } // namespace detail

    /// TokenService 私有构造函数（单例模式）
    TokenService::TokenService()
        : m_jwt_secret(),
          m_jwt_verifier(BuildJwtVerifier("")),
          m_share_jwt_verifier(BuildShareJwtVerifier("")),
          m_redis_service(RedisService::GetInstance()) {
        Logger::Debug(AuthRuntimeLogContext()) << "Token service constructed";
    }

    void TokenService::Initialize(std::string jwt_secret) {
        auto instance = GetInstance();
        instance->m_jwt_secret = std::move(jwt_secret);
        instance->m_jwt_verifier = BuildJwtVerifier(instance->m_jwt_secret);
        instance->m_share_jwt_verifier = BuildShareJwtVerifier(instance->m_jwt_secret);
    }

    auto TokenService::GenerateTokens(
        uint64_t user_id,
        const std::string& username,
        int role,
        int status,
        disk::utils::LogContext log_context
    ) const
        -> std::pair<std::string, std::string> {

        const auto now = std::chrono::system_clock::now();

        /// 定义 traits 别名
        using traits = jwt::traits::open_source_parsers_jsoncpp;

        /// 生成 JTI (Access Token 和 Refresh Token 各有独立的 JTI)
        const auto access_jti = drogon::utils::getUuid();
        const auto refresh_jti = drogon::utils::getUuid();

        /// 生成 Access Token (带唯一 JTI)
        jwt::builder<jwt::default_clock, traits> access_builder{ jwt::default_clock{} };
        auto access_token =
            access_builder.set_issuer("disk")
                .set_type("JWT")
                .set_subject(std::to_string(user_id))
                .set_payload_claim("username", username)
                .set_payload_claim("type", "access")
                .set_payload_claim("jti", access_jti)
                .set_payload_claim("role", role)
                .set_payload_claim("status", status)
                .set_issued_at(now)
                .set_expires_at(now + std::chrono::seconds(GetAccessTokenExpireSeconds()))
                .sign(jwt::algorithm::hs256{ m_jwt_secret });

        /// 生成 Refresh Token (带唯一 JTI)
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
        Logger::Debug(log_context)
            << "Generating token pair: user_id=" << user_id << ", access_jti=" << access_jti
            << ", refresh_jti=" << refresh_jti;
        return { access_token, refresh_token };
    }

    auto TokenService::VerifyAccessToken(
        const std::string& token,
        disk::utils::LogContext log_context
    ) const
        -> Result<AccessTokenClaims> {

        g_pool_metrics.OnSubmit();
        const auto start = std::chrono::steady_clock::now();

        auto cleanup = [start, log_context]() {
            g_pool_metrics.OnComplete();
            const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                        std::chrono::steady_clock::now() - start
            )
                                        .count();
            Logger::Info(log_context)
                << "[auth_cpu_pool] op=jwt_verify duration_us=" << elapsed_us;
        };

        try {
            auto decoded = jwt::decode<JwtTraits>(token);

            m_jwt_verifier.verify(decoded);

            const auto type = decoded.get_payload_claim("type").as_string();
            if (type != "access") {
                cleanup();
                return std::unexpected(ErrorInfo(disk::error::Code::TokenWrongType));
            }

            const auto user_id_str = decoded.get_subject();
            const auto username = decoded.get_payload_claim("username").as_string();
            const auto jti = decoded.get_payload_claim("jti").as_string();

            const auto user_id = std::stoull(user_id_str);

            int token_role = 0;
            if (decoded.has_payload_claim("role")) {
                token_role = static_cast<int>(decoded.get_payload_claim("role").as_integer());
            }

            int token_status = 1;
            if (decoded.has_payload_claim("status")) {
                token_status = static_cast<int>(decoded.get_payload_claim("status").as_integer());
            }

            cleanup();

            Logger::Trace(log_context)
                << "JWT verification successful: user_id=" << user_id
                << ", username=" << username << ", jti=" << jti
                << ", role=" << token_role << ", status=" << token_status;
            return AccessTokenClaims{ .user_id = user_id, .username = username, .jti = jti, .role = token_role, .status = token_status };

        } catch (const jwt::error::token_verification_exception& error) {
            cleanup();
            Logger::Warn(log_context) << "JWT verification failed";
            if (IsTokenExpired(error)) {
                return std::unexpected(ErrorInfo(disk::error::Code::TokenExpired));
            }
            return std::unexpected(ErrorInfo(disk::error::Code::InvalidToken));
        } catch (const std::exception&) {
            cleanup();
            Logger::Warn(log_context) << "JWT parsing failed";
            return std::unexpected(ErrorInfo(disk::error::Code::TokenMalformed));
        }
    }

    auto TokenService::VerifyRefreshToken(
        const std::string& token,
        disk::utils::LogContext log_context
    ) const
        -> Result<std::pair<uint64_t, std::string>> {

        g_pool_metrics.OnSubmit();
        const auto start = std::chrono::steady_clock::now();

        auto cleanup = [start, log_context]() {
            g_pool_metrics.OnComplete();
            const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                        std::chrono::steady_clock::now() - start
            )
                                        .count();
            Logger::Info(log_context)
                << "[auth_cpu_pool] op=jwt_refresh_verify duration_us=" << elapsed_us;
        };

        try {
            auto decoded = jwt::decode<JwtTraits>(token);

            m_jwt_verifier.verify(decoded);

            const auto type = decoded.get_payload_claim("type").as_string();
            if (type != "refresh") {
                cleanup();
                return std::unexpected(ErrorInfo(disk::error::Code::TokenWrongType));
            }

            const auto jti = decoded.get_payload_claim("jti").as_string();

            const auto user_id_str = decoded.get_subject();
            const auto user_id = std::stoull(user_id_str);

            cleanup();

            Logger::Trace(log_context)
                << "Refresh token verification successful: user_id=" << user_id
                << ", jti=" << jti;
            return std::make_pair(user_id, jti);

        } catch (const jwt::error::token_verification_exception& error) {
            cleanup();
            Logger::Warn(log_context) << "Refresh token verification failed";
            if (IsTokenExpired(error)) {
                return std::unexpected(ErrorInfo(disk::error::Code::TokenExpired));
            }
            return std::unexpected(ErrorInfo(disk::error::Code::InvalidRefreshToken));
        } catch (const std::exception&) {
            cleanup();
            Logger::Warn(log_context) << "Refresh token parsing failed";
            return std::unexpected(ErrorInfo(disk::error::Code::TokenMalformed));
        }
    }

    auto TokenService::StoreRefreshToken(
        uint64_t user_id,
        const std::string& refresh_token,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<void>> {
        const auto key = disk::redis::RedisKeyPrefix::BuildRefreshTokenKey(user_id);

        auto hash_result = disk::utils::HashUtil::HashToken(refresh_token);
        if (!hash_result) {
            co_return std::unexpected(hash_result.error());
        }
        const auto hash = disk::utils::HashUtil::TokenHashToHex(hash_result.value());

        co_return co_await m_redis_service->Set(key, hash, REFRESH_TOKEN_TTL, log_context);
    }

    auto TokenService::RefreshRefreshToken(
        uint64_t user_id,
        const std::string& old_token,
        const std::string& new_token,
        disk::utils::LogContext log_context
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

        /// Atomic CAS: compare and swap in one operation
        /// Uses Lua script to ensure exactly one success under concurrency
        auto cas_result = co_await m_redis_service->CompareAndSwap(
            key,
            old_hash,
            new_hash,
            REFRESH_TOKEN_TTL,
            log_context
        );

        if (!cas_result) {
            Logger::Error(log_context) << "Redis CAS operation failed";
            co_return std::unexpected(cas_result.error());
        }

        if (!cas_result.value()) {
            Logger::Warn(log_context) << "Refresh token already used or refreshed";
            co_return std::unexpected(ErrorInfo(disk::error::Code::RefreshTokenAlreadyUsed));
        }

        Logger::Debug(log_context) << "Refresh token rotated successfully";
        co_return {};
    }

    auto TokenService::InvalidateAccessToken(
        const std::string& token,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<void>> {
        auto jti_result = ExtractJti(token, log_context);
        if (!jti_result) {
            co_return std::unexpected(jti_result.error());
        }

        const auto jti = jti_result.value();

        const auto key = disk::redis::RedisKeyPrefix::BuildAccessTokenBlacklistKey(jti);
        auto result = co_await m_redis_service->Set(key, "1", ACCESS_TOKEN_TTL, log_context);
        if (!result) {
            co_return std::unexpected(result.error());
        }

        const auto now = std::chrono::steady_clock::now();
        std::unique_lock lock(m_cache_mutex);
        m_revocation_cache.Upsert(jti, RevocationCacheEntry{ .expires_at = now + std::chrono::seconds(GetAccessTokenExpireSeconds()) });
        co_return {};
    }

    auto TokenService::RevokeRefreshToken(
        uint64_t user_id,
        disk::utils::LogContext log_context
    ) -> drogon::Task<Result<void>> {
        const auto key = disk::redis::RedisKeyPrefix::BuildRefreshTokenKey(user_id);
        co_return co_await m_redis_service->Delete(key, log_context);
    }

    auto TokenService::IsAccessTokenRevoked(
        const std::string& jti,
        disk::utils::LogContext log_context
    ) -> drogon::Task<Result<bool>> {
        const auto now = std::chrono::steady_clock::now();

        {
            std::shared_lock lock(m_cache_mutex);
            auto* entry = m_revocation_cache.Find(jti, now);
            if (entry != nullptr) {
                co_return true;
            }
        }

        const auto key = disk::redis::RedisKeyPrefix::BuildAccessTokenBlacklistKey(jti);
        auto revoked = co_await m_redis_service->Exists(key, log_context);
        if (!revoked) {
            co_return std::unexpected(revoked.error());
        }

        if (revoked.value()) {
            std::unique_lock lock(m_cache_mutex);
            m_revocation_cache.Upsert(jti, RevocationCacheEntry{ .expires_at = now + std::chrono::seconds(GetAccessTokenExpireSeconds()) });
        }

        co_return revoked.value();
    }

    auto TokenService::ClearRevocationCache() -> void {
        std::unique_lock lock(m_cache_mutex);
        m_revocation_cache.Clear();
    }

    auto TokenService::SetRevocationCacheEntryForTest(
        const std::string& jti,
        bool is_revoked,
        int ttl_seconds
    ) -> void {
        std::unique_lock lock(m_cache_mutex);
        if (!is_revoked) {
            m_revocation_cache.Erase(jti);
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        m_revocation_cache.Upsert(jti, RevocationCacheEntry{ .expires_at = now + std::chrono::seconds(ttl_seconds) });
    }

    auto TokenService::GetRevocationCacheSizeForTest() const -> size_t {
        std::shared_lock lock(m_cache_mutex);
        return m_revocation_cache.Size();
    }

    auto TokenService::StartCacheMaintenance() -> void {
        drogon::app().getLoop()->runEvery(
            CACHE_MAINTENANCE_INTERVAL_SECONDS,
            [this]() { EvictExpiredCacheEntries(); }
        );
        Logger::Debug(AuthRuntimeLogContext()) << "Revocation cache maintenance started: interval_seconds="
                                               << CACHE_MAINTENANCE_INTERVAL_SECONDS;

        const auto& json = drogon::app().getCustomConfig();
        int metrics_interval = 60;
        if (json.isMember("disk") && json["disk"].isMember("auth_cpu_pool_metrics_interval_seconds")) {
            metrics_interval = json["disk"]["auth_cpu_pool_metrics_interval_seconds"].asInt();
            if (metrics_interval <= 0) {
                metrics_interval = 60;
            }
        }
        StartPoolMetricsTimer(metrics_interval);
    }

    auto TokenService::StartPoolMetricsTimer(int interval_seconds) -> void {
        m_metrics_last_reset = std::chrono::steady_clock::now();

        drogon::app().getLoop()->runEvery(
            static_cast<double>(interval_seconds),
            [this]() { LogPoolMetrics(); }
        );
        Logger::Info(AuthRuntimeLogContext()) << "Auth CPU pool metrics started: interval_seconds="
                                              << interval_seconds;
    }

    auto TokenService::LogPoolMetrics() -> void {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_s = std::chrono::duration_cast<std::chrono::seconds>(
                                   now - m_metrics_last_reset
        )
                                   .count();

        const auto submitted = g_pool_metrics.total_submitted.exchange(0, std::memory_order_relaxed);
        const auto completed = g_pool_metrics.total_completed.exchange(0, std::memory_order_relaxed);
        const auto active = g_pool_metrics.active_tasks.load(std::memory_order_relaxed);
        const auto peak = g_pool_metrics.peak_active.exchange(0, std::memory_order_relaxed);

        Logger::Info(AuthRuntimeLogContext()) << "Auth CPU pool metrics: period_seconds=" << elapsed_s
                                              << ", submitted=" << submitted
                                              << ", completed=" << completed
                                              << ", active=" << active
                                              << ", peak=" << peak;

        m_metrics_last_reset = now;
    }

    auto TokenService::EvictExpiredCacheEntries() const -> void {
        const auto now = std::chrono::steady_clock::now();
        size_t access_evicted = 0;
        size_t access_remaining = 0;
        {
            std::unique_lock lock(m_cache_mutex);
            access_evicted = m_revocation_cache.EvictExpired(now);
            access_remaining = m_revocation_cache.Size();
        }
        size_t share_evicted = 0;
        size_t share_remaining = 0;
        {
            std::unique_lock lock(m_share_cache_mutex);
            share_evicted = m_share_revocation_cache.EvictExpired(now);
            share_remaining = m_share_revocation_cache.Size();
        }
        Logger::Debug(AuthRuntimeLogContext()) << "Token cache eviction completed: access_evicted=" << access_evicted
                                               << ", access_size=" << access_remaining
                                               << ", share_evicted=" << share_evicted
                                               << ", share_size=" << share_remaining;
    }

    auto TokenService::ExtractJti(
        const std::string& token,
        disk::utils::LogContext log_context
    ) const -> Result<std::string> {
        using traits = jwt::traits::open_source_parsers_jsoncpp;

        try {
            auto decoded = jwt::decode<traits>(token);

            /// 检查 JTI claim 是否存在（access 和 refresh token 现在都有 JTI）
            if (decoded.has_payload_claim("jti")) {
                const auto jti = decoded.get_payload_claim("jti");
                return jti.as_string();
            }

            Logger::Warn(log_context) << "Token missing JTI claim";
            return std::unexpected(ErrorInfo(disk::error::Code::TokenMalformed));
        } catch (const jwt::error::token_verification_exception&) {
            Logger::Warn(log_context) << "Failed to extract JTI";
            return std::unexpected(ErrorInfo(disk::error::Code::TokenMalformed));
        } catch (const std::exception&) {
            Logger::Warn(log_context) << "Failed to extract JTI";
            return std::unexpected(ErrorInfo(disk::error::Code::TokenMalformed));
        }
    }

    /// ==================== Share Token 静态方法 ====================

    auto TokenService::GenerateShareToken(
        const std::string& jwt_secret,
        const std::string& share_code,
        uint64_t share_id,
        const std::string& permission,
        disk::utils::LogContext log_context
    ) -> Result<std::string> {
        using traits = jwt::traits::open_source_parsers_jsoncpp;

        if (!IsValidSharePermission(permission)) {
            return std::unexpected(
                ErrorInfo(disk::error::Code::InvalidParameter, "Invalid share permission")
            );
        }

        const auto now = std::chrono::system_clock::now();
        const auto jti = drogon::utils::getUuid();

        try {
            Json::Value scope(Json::objectValue);
            scope["share_id"] = share_code;
            scope["permission"] = permission;

            jwt::builder<jwt::default_clock, traits> builder{ jwt::default_clock{} };
            auto token =
                builder.set_issuer("disk_share")
                    .set_type("JWT")
                    .set_subject(std::to_string(share_id))
                    .set_payload_claim("share_code", share_code)
                    .set_payload_claim("type", "share")
                    .set_payload_claim("jti", jti)
                    .set_payload_claim("scope", scope)
                    .set_issued_at(now)
                    .set_expires_at(now + std::chrono::seconds(GetShareTokenExpireSeconds()))
                    .sign(jwt::algorithm::hs256{ jwt_secret });

            Logger::Debug(log_context) << "Share token generated successfully";
            return token;
        } catch (const std::exception&) {
            Logger::Error(log_context) << "Failed to generate share token";
            return std::unexpected(
                ErrorInfo(disk::error::Code::InternalError, "Token generation failed")
            );
        }
    }

    auto TokenService::VerifyShareToken(
        const std::string& jwt_secret,
        const std::string& token,
        disk::utils::LogContext log_context
    )
        -> Result<ShareTokenClaims> {
        if (token.empty()) {
            return std::unexpected(ErrorInfo(disk::error::Code::TokenMalformed, "Token is empty"));
        }

        try {
            auto decoded = jwt::decode<JwtTraits>(token);

            const auto token_service = GetInstance();
            if (token_service->m_jwt_secret == jwt_secret) {
                token_service->m_share_jwt_verifier.verify(decoded);
            } else {
                BuildShareJwtVerifier(jwt_secret).verify(decoded);
            }

            const auto type = decoded.get_payload_claim("type").as_string();
            if (type != "share") {
                return std::unexpected(ErrorInfo(disk::error::Code::TokenWrongType));
            }

            const auto share_code = decoded.get_payload_claim("share_code").as_string();
            const auto jti = decoded.get_payload_claim("jti").as_string();
            const auto share_id_str = decoded.get_subject();
            std::size_t parsed_length = 0;
            const auto share_id = std::stoull(share_id_str, &parsed_length);
            if (share_code.empty() || jti.empty() || share_id == 0 || parsed_length != share_id_str.size()) {
                return std::unexpected(ErrorInfo(disk::error::Code::TokenMalformed));
            }

            const auto scope = decoded.get_payload_claim("scope").to_json();
            if (!scope.isObject() || !scope.isMember("share_id") ||
                !scope["share_id"].isString() || !scope.isMember("permission") ||
                !scope["permission"].isString()) {
                return std::unexpected(ErrorInfo(disk::error::Code::TokenMalformed));
            }

            const auto scope_share_id = scope["share_id"].asString();
            const auto scope_permission = scope["permission"].asString();
            if (scope_share_id != share_code || !IsValidSharePermission(scope_permission)) {
                return std::unexpected(ErrorInfo(disk::error::Code::TokenMalformed));
            }

            Logger::Debug(log_context)
                << "Share token verification successful: share_code=" << share_code
                << ", share_id=" << share_id;
            return ShareTokenClaims{
                .share_code = share_code,
                .share_id = share_id,
                .jti = jti,
                .scope = {
                          .share_id = scope_share_id,
                          .permission = scope_permission,
                          },
            };

        } catch (const jwt::error::token_verification_exception& error) {
            Logger::Warn(log_context) << "Share token verification failed";
            if (IsTokenExpired(error)) {
                return std::unexpected(ErrorInfo(disk::error::Code::TokenExpired));
            }
            return std::unexpected(ErrorInfo(disk::error::Code::TokenMalformed));
        } catch (const std::exception&) {
            Logger::Warn(log_context) << "Share token parsing failed";
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

    /// ==================== Share Token Redis 异步方法 ====================

    auto TokenService::VerifyShareTokenWithRedis(
        const std::string& share_code,
        const std::string& token,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<ShareTokenClaims>> {
        auto verify_result = VerifyShareToken(m_jwt_secret, token, log_context);
        if (!verify_result) {
            co_return std::unexpected(verify_result.error());
        }

        auto hash_result = ExtractShareTokenHash(token);
        if (!hash_result) {
            co_return std::unexpected(hash_result.error());
        }

        auto revoked = co_await IsShareTokenRevoked(hash_result.value(), log_context);
        if (!revoked) {
            co_return std::unexpected(revoked.error());
        }
        if (revoked.value()) {
            Logger::Warn(log_context) << "Share token has been revoked: share_code=" << share_code;
            co_return std::unexpected(ErrorInfo(disk::error::Code::TokenRevoked));
        }

        co_return verify_result.value();
    }

    auto TokenService::IsShareTokenRevoked(
        const std::string& token_hash,
        disk::utils::LogContext log_context
    ) -> drogon::Task<Result<bool>> {
        const auto now = std::chrono::steady_clock::now();

        {
            std::shared_lock lock(m_share_cache_mutex);
            auto* entry = m_share_revocation_cache.Find(token_hash, now);
            if (entry != nullptr) {
                co_return true;
            }
        }

        const auto key = disk::redis::RedisKeyPrefix::BuildShareTokenBlacklistKey(token_hash);
        auto revoked = co_await m_redis_service->Exists(key, log_context);
        if (!revoked) {
            co_return std::unexpected(revoked.error());
        }

        if (revoked.value()) {
            std::unique_lock lock(m_share_cache_mutex);
            m_share_revocation_cache.Upsert(token_hash, ShareCacheEntry{ .expires_at = now + std::chrono::seconds(GetShareTokenExpireSeconds()) });
        }

        co_return revoked.value();
    }

    auto TokenService::SetShareRevocationCacheEntryForTest(
        const std::string& token_hash,
        bool is_revoked,
        int ttl_seconds
    ) -> void {
        std::unique_lock lock(m_share_cache_mutex);
        if (!is_revoked) {
            m_share_revocation_cache.Erase(token_hash);
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        m_share_revocation_cache.Upsert(token_hash, ShareCacheEntry{ .expires_at = now + std::chrono::seconds(ttl_seconds) });
    }

    auto TokenService::GetShareRevocationCacheSizeForTest() const -> size_t {
        std::shared_lock lock(m_share_cache_mutex);
        return m_share_revocation_cache.Size();
    }

    auto TokenService::ClearShareRevocationCache() -> void {
        std::unique_lock lock(m_share_cache_mutex);
        m_share_revocation_cache.Clear();
    }

} // namespace disk::services
