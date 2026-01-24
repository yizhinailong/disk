/**
 * @file TokenService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief JWT令牌服务实现
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

#include "services/RedisService.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::auth {

    TokenService::TokenService(std::string jwt_secret, disk::services::RedisService& redis_service)
        : m_jwt_secret(std::move(jwt_secret)),
          m_redis_service(redis_service) {
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
                return std::unexpected(ErrorInfo(ErrorCode::TokenWrongType));
            }

            const auto user_id_str = decoded.get_subject();
            const auto username = decoded.get_payload_claim("username").as_string();

            const auto user_id = std::stoull(user_id_str);

            LOG_DEBUG << "JWT 验证成功: user_id=" << user_id << ", username=" << username;
            return std::make_pair(user_id, username);

        } catch (const jwt::error::token_verification_exception& e) {
            LOG_WARN << "JWT 验证失败: " << e.what();
            if (std::string(e.what()).find("expired") != std::string::npos) {
                return std::unexpected(ErrorInfo(ErrorCode::TokenExpired));
            }
            return std::unexpected(ErrorInfo(ErrorCode::InvalidToken));
        } catch (const std::exception& e) {
            LOG_WARN << "JWT 解析失败: " << e.what();
            return std::unexpected(ErrorInfo(ErrorCode::TokenMalformed));
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
                return std::unexpected(ErrorInfo(ErrorCode::TokenWrongType));
            }

            const auto jti = decoded.get_payload_claim("jti").as_string();

            const auto user_id_str = decoded.get_subject();
            const auto user_id = std::stoull(user_id_str);

            LOG_DEBUG << "刷新令牌验证成功: user_id=" << user_id << ", jti=" << jti;
            return std::make_pair(user_id, jti);

        } catch (const jwt::error::token_verification_exception& e) {
            LOG_WARN << "刷新令牌验证失败: " << e.what();
            if (std::string(e.what()).find("expired") != std::string::npos) {
                return std::unexpected(ErrorInfo(ErrorCode::TokenExpired));
            }
            return std::unexpected(ErrorInfo(ErrorCode::InvalidRefreshToken));
        } catch (const std::exception& e) {
            LOG_WARN << "刷新令牌解析失败: " << e.what();
            return std::unexpected(ErrorInfo(ErrorCode::TokenMalformed));
        }
    }

    auto TokenService::StoreRefreshToken(uint64_t user_id, const std::string& refresh_token)
        -> drogon::Task<Result<void>> {
        co_return co_await m_redis_service.StoreRefreshToken(user_id, refresh_token);
    }

    auto TokenService::RefreshRefreshToken(
        uint64_t user_id,
        const std::string& old_token,
        const std::string& new_token
    ) -> drogon::Task<Result<void>> {
        co_return co_await m_redis_service.RefreshRefreshToken(user_id, old_token, new_token);
    }

    auto TokenService::InvalidateAccessToken(const std::string& token) -> drogon::Task<Result<void>> {
        co_return co_await m_redis_service.InvalidateAccessToken(token);
    }

    auto TokenService::RevokeRefreshToken(uint64_t user_id) -> drogon::Task<Result<void>> {
        co_return co_await m_redis_service.RevokeRefreshToken(user_id);
    }

    auto TokenService::IsAccessTokenRevoked(const std::string& jti) -> drogon::Task<bool> {
        co_return co_await m_redis_service.IsAccessTokenRevoked(jti);
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
            return std::unexpected(ErrorInfo(ErrorCode::TokenMalformed));
        } catch (const jwt::error::token_verification_exception& e) {
            LOG_WARN << "提取 JTI 失败: " << e.what();
            return std::unexpected(ErrorInfo(ErrorCode::TokenMalformed));
        } catch (const std::exception& e) {
            LOG_WARN << "提取 JTI 失败: " << e.what();
            return std::unexpected(ErrorInfo(ErrorCode::TokenMalformed));
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
                return std::unexpected(ErrorInfo(ErrorCode::TokenExpired));
            }

            return static_cast<int>(remaining);
        } catch (const std::exception& e) {
            LOG_WARN << "计算 TTL 失败: " << e.what();
            return std::unexpected(ErrorInfo(ErrorCode::TokenMalformed));
        }
    }

} // namespace disk::auth
