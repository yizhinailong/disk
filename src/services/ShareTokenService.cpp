/**
 * @file ShareTokenService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 分享令牌服务实现
 * @version 0.1
 * @date 2026-02-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "ShareTokenService.hpp"

#include <drogon/utils/Utilities.h>
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/traits.h>

#include "utils/HashUtil.hpp"
#include "utils/RedisKeyPrefix.hpp"

namespace disk::services {

    using ErrorInfo = disk::error::ErrorInfo;

    static constexpr int SHARE_TOKEN_TTL = 3600;

    ShareTokenService::ShareTokenService(std::string jwt_secret, drogon::nosql::RedisClientPtr redis_client)
        : m_jwt_secret(std::move(jwt_secret)),
          m_redis_client(std::move(redis_client)),
          m_redis_service(std::make_shared<RedisService>(m_redis_client)) {
        LOG_DEBUG << "ShareTokenService 初始化完成";
    }

    ShareTokenService::ShareTokenService(std::string jwt_secret, std::shared_ptr<RedisService> redis_service)
        : m_jwt_secret(std::move(jwt_secret)),
          m_redis_client(nullptr),
          m_redis_service(std::move(redis_service)) {
        LOG_DEBUG << "ShareTokenService 初始化完成";
    }

    auto ShareTokenService::GenerateToken(
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
            return std::unexpected(ErrorInfo(ErrorCode::InternalError, "令牌生成失败"));
        }
    }

    auto ShareTokenService::VerifyToken(const std::string& jwt_secret, const std::string& token)
        -> Result<ShareTokenClaims> {
        using traits = jwt::traits::open_source_parsers_jsoncpp;

        if (token.empty()) {
            return std::unexpected(ErrorInfo(ErrorCode::TokenMalformed, "令牌为空"));
        }

        try {
            auto decoded = jwt::decode<traits>(token);

            auto verifier = jwt::verify<traits>()
                                .allow_algorithm(jwt::algorithm::hs256{ jwt_secret })
                                .with_issuer("disk_share");

            verifier.verify(decoded);

            const auto type = decoded.get_payload_claim("type").as_string();
            if (type != "share") {
                return std::unexpected(ErrorInfo(ErrorCode::TokenWrongType));
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
                return std::unexpected(ErrorInfo(ErrorCode::TokenExpired));
            }
            return std::unexpected(ErrorInfo(ErrorCode::TokenMalformed));
        } catch (const std::exception& e) {
            LOG_WARN << "分享令牌解析失败: " << e.what();
            return std::unexpected(ErrorInfo(ErrorCode::TokenMalformed));
        }
    }

    auto ShareTokenService::ExtractTokenHash(const std::string& token) -> Result<std::string> {
        auto hash_result = disk::utils::HashUtil::HashToken(token);
        if (!hash_result) {
            return std::unexpected(hash_result.error());
        }
        return disk::utils::HashUtil::TokenHashToHex(hash_result.value());
    }

    auto ShareTokenService::StoreShareToken(const std::string& share_code, const std::string& token)
        -> drogon::Task<Result<void>> {
        auto hash_result = ExtractTokenHash(token);
        if (!hash_result) {
            co_return std::unexpected(hash_result.error());
        }

        const auto key = disk::redis::RedisKeyPrefix::BuildShareTokenKey(share_code, hash_result.value());
        co_return co_await m_redis_service->Set(key, "1", SHARE_TOKEN_TTL);
    }

    auto ShareTokenService::VerifyShareToken(const std::string& share_code, const std::string& token)
        -> drogon::Task<Result<ShareTokenClaims>> {
        auto verify_result = VerifyToken(m_jwt_secret, token);
        if (!verify_result) {
            co_return std::unexpected(verify_result.error());
        }

        auto hash_result = ExtractTokenHash(token);
        if (!hash_result) {
            co_return std::unexpected(hash_result.error());
        }

        if (co_await IsShareTokenRevoked(hash_result.value())) {
            LOG_WARN << "分享令牌已被撤销: share_code=" << share_code;
            co_return std::unexpected(ErrorInfo(ErrorCode::TokenRevoked));
        }

        co_return verify_result.value();
    }

    auto ShareTokenService::RevokeShareToken(const std::string& token) -> drogon::Task<Result<void>> {
        auto hash_result = ExtractTokenHash(token);
        if (!hash_result) {
            co_return std::unexpected(hash_result.error());
        }

        const auto key = disk::redis::RedisKeyPrefix::BuildShareTokenBlacklistKey(hash_result.value());
        co_return co_await m_redis_service->Set(key, "1", SHARE_TOKEN_TTL);
    }

    auto ShareTokenService::IsShareTokenRevoked(const std::string& token_hash) -> drogon::Task<bool> {
        const auto key = disk::redis::RedisKeyPrefix::BuildShareTokenBlacklistKey(token_hash);
        co_return co_await m_redis_service->Exists(key);
    }

} // namespace disk::services
