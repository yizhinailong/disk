/**
 * @file TokenService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 统一JWT令牌服务实现
 * @version 0.1
 * @date 2026-01-16
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "TokenService.hpp"

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

    // TokenService构造函数（内部创建RedisService）
    TokenService::TokenService(std::string jwt_secret, drogon::nosql::RedisClientPtr redis_client)
        : m_jwt_secret(std::move(jwt_secret)),
          m_redis_client(std::move(redis_client)),
          m_redis_service(std::make_shared<disk::services::RedisService>(m_redis_client)) {
        LOG_DEBUG << "TokenService 初始化完成";
    }

    // TokenService构造函数（外部提供RedisService）
    TokenService::TokenService(std::string jwt_secret, std::shared_ptr<disk::services::RedisService> redis_service)
        : m_jwt_secret(std::move(jwt_secret)),
          m_redis_client(nullptr),
          m_redis_service(std::move(redis_service)) {
        LOG_DEBUG << "TokenService 初始化完成";
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
        auto access_token = access_builder
                                .set_issuer("disk")
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
        auto refresh_token = refresh_builder
                                 .set_issuer("disk")
                                 .set_type("JWT")
                                 .set_subject(std::to_string(user_id))
                                 .set_payload_claim("type", "refresh")
                                 .set_payload_claim("jti", refresh_jti)
                                 .set_issued_at(now)
                                 .set_expires_at(now + std::chrono::seconds(GetRefreshTokenExpireSeconds()))
                                 .sign(jwt::algorithm::hs256{ m_jwt_secret });

        LOG_DEBUG << "生成令牌对: user_id=" << user_id
                  << ", access_jti=" << access_jti
                  << ", refresh_jti=" << refresh_jti;
        return { access_token, refresh_token };
    }

    auto TokenService::VerifyAccessToken(const std::string& token) const
        -> Result<std::pair<uint64_t, std::string>> {

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

            const auto user_id = std::stoull(user_id_str);

            LOG_DEBUG << "JWT 验证成功: user_id=" << user_id << ", username=" << username;
            return std::make_pair(user_id, username);

        } catch (const jwt::error::token_verification_exception& e) {
            LOG_WARN << "JWT 验证失败: " << e.what();
            if (std::string(e.what()).find("expired") != std::string::npos) {
                return std::unexpected(ErrorInfo(disk::error::Code::TokenExpired));
            }
            return std::unexpected(ErrorInfo(disk::error::Code::InvalidToken));
        } catch (const std::exception& e) {
            LOG_WARN << "JWT 解析失败: " << e.what();
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

            LOG_DEBUG << "刷新令牌验证成功: user_id=" << user_id << ", jti=" << jti;
            return std::make_pair(user_id, jti);

        } catch (const jwt::error::token_verification_exception& e) {
            LOG_WARN << "刷新令牌验证失败: " << e.what();
            if (std::string(e.what()).find("expired") != std::string::npos) {
                return std::unexpected(ErrorInfo(disk::error::Code::TokenExpired));
            }
            return std::unexpected(ErrorInfo(disk::error::Code::InvalidRefreshToken));
        } catch (const std::exception& e) {
            LOG_WARN << "刷新令牌解析失败: " << e.what();
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

        auto get_result = co_await m_redis_service->Get(key);
        if (!get_result) {
            LOG_WARN << "Refresh token 不存在: user_id=" << user_id;
            co_return std::unexpected(ErrorInfo(disk::error::Code::InvalidRefreshToken));
        }

        const auto& current_hash = get_result.value();
        if (current_hash != old_hash) {
            LOG_WARN << "Refresh token 已被使用或已刷新: user_id=" << user_id;
            co_return std::unexpected(ErrorInfo(disk::error::Code::RefreshTokenAlreadyUsed));
        }

        co_return co_await m_redis_service->Set(key, new_hash, REFRESH_TOKEN_TTL);
    }

    auto TokenService::InvalidateAccessToken(const std::string& token) -> drogon::Task<Result<void>> {
        auto jti_result = ExtractJti(token);
        if (!jti_result) {
            co_return std::unexpected(jti_result.error());
        }

        const auto jti = jti_result.value();
        const auto key = disk::redis::RedisKeyPrefix::BuildAccessTokenBlacklistKey(jti);

        co_return co_await m_redis_service->Set(key, "1", ACCESS_TOKEN_TTL);
    }

    auto TokenService::RevokeRefreshToken(uint64_t user_id) -> drogon::Task<Result<void>> {
        const auto key = disk::redis::RedisKeyPrefix::BuildRefreshTokenKey(user_id);
        co_return co_await m_redis_service->Delete(key);
    }

    auto TokenService::IsAccessTokenRevoked(const std::string& jti) -> drogon::Task<bool> {
        const auto key = disk::redis::RedisKeyPrefix::BuildAccessTokenBlacklistKey(jti);
        co_return co_await m_redis_service->Exists(key);
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

            LOG_WARN << "令牌缺少 JTI claim";
            return std::unexpected(ErrorInfo(disk::error::Code::TokenMalformed));
        } catch (const jwt::error::token_verification_exception& e) {
            LOG_WARN << "提取 JTI 失败: " << e.what();
            return std::unexpected(ErrorInfo(disk::error::Code::TokenMalformed));
        } catch (const std::exception& e) {
            LOG_WARN << "提取 JTI 失败: " << e.what();
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
            LOG_WARN << "计算 TTL 失败: " << e.what();
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
            auto token = builder
                             .set_issuer("disk_share")
                             .set_type("JWT")
                             .set_subject(std::to_string(share_id))
                             .set_payload_claim("share_code", share_code)
                             .set_payload_claim("type", "share")
                             .set_payload_claim("jti", jti)
                             .set_issued_at(now)
                             .set_expires_at(now + std::chrono::seconds(GetShareTokenExpireSeconds()))
                             .sign(jwt::algorithm::hs256{ jwt_secret });

            LOG_DEBUG << "生成分享令牌: share_code=" << share_code << ", share_id=" << share_id;
            return token;
        } catch (const std::exception& e) {
            LOG_ERROR << "生成分享令牌失败: " << e.what();
            return std::unexpected(ErrorInfo(disk::error::Code::InternalError, "令牌生成失败"));
        }
    }

    auto TokenService::VerifyShareToken(const std::string& jwt_secret, const std::string& token)
        -> Result<ShareTokenClaims> {
        using traits = jwt::traits::open_source_parsers_jsoncpp;

        if (token.empty()) {
            return std::unexpected(ErrorInfo(disk::error::Code::TokenMalformed, "令牌为空"));
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

            LOG_DEBUG << "分享令牌验证成功: share_code=" << share_code << ", share_id=" << share_id;
            return ShareTokenClaims{
                .share_code = share_code,
                .share_id = share_id,
                .jti = jti
            };

        } catch (const jwt::error::token_verification_exception& e) {
            LOG_WARN << "分享令牌验证失败: " << e.what();
            if (std::string(e.what()).find("expired") != std::string::npos) {
                return std::unexpected(ErrorInfo(disk::error::Code::TokenExpired));
            }
            return std::unexpected(ErrorInfo(disk::error::Code::TokenMalformed));
        } catch (const std::exception& e) {
            LOG_WARN << "分享令牌解析失败: " << e.what();
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

    auto TokenService::StoreShareToken(const std::string& share_code, const std::string& token)
        -> drogon::Task<Result<void>> {
        auto hash_result = ExtractShareTokenHash(token);
        if (!hash_result) {
            co_return std::unexpected(hash_result.error());
        }

        const auto key = disk::redis::RedisKeyPrefix::BuildShareTokenKey(share_code, hash_result.value());
        co_return co_await m_redis_service->Set(key, "1", SHARE_TOKEN_TTL);
    }

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
            LOG_WARN << "分享令牌已被撤销: share_code=" << share_code;
            co_return std::unexpected(ErrorInfo(disk::error::Code::TokenRevoked));
        }

        co_return verify_result.value();
    }

    auto TokenService::RevokeShareToken(const std::string& token) -> drogon::Task<Result<void>> {
        auto hash_result = ExtractShareTokenHash(token);
        if (!hash_result) {
            co_return std::unexpected(hash_result.error());
        }

        const auto key = disk::redis::RedisKeyPrefix::BuildShareTokenBlacklistKey(hash_result.value());
        co_return co_await m_redis_service->Set(key, "1", SHARE_TOKEN_TTL);
    }

    auto TokenService::IsShareTokenRevoked(const std::string& token_hash) -> drogon::Task<bool> {
        const auto key = disk::redis::RedisKeyPrefix::BuildShareTokenBlacklistKey(token_hash);
        co_return co_await m_redis_service->Exists(key);
    }

} // namespace disk::services
