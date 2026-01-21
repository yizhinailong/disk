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
#include "services/RedisService.hpp" // 新增
#include "utils/ErrorCode.hpp"
#include "utils/HashUtil.hpp"

namespace disk::auth {

    using disk::utils::HashUtil;

    TokenService::TokenService(std::string jwt_secret, drogon::nosql::RedisClientPtr redis_client)
        : m_jwt_secret(std::move(jwt_secret)),
          m_redis_client(std::move(redis_client)) {}

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

    auto TokenService::StoreRefreshToken(uint64_t user_id, const std::string& refresh_token) const
        -> drogon::Task<bool> {

        const auto key = "refresh_token:" + std::to_string(user_id);

        auto hash_result = HashUtil::HashToken(refresh_token);
        if (!hash_result) {
            co_return false;
        }
        const auto hash = HashUtil::TokenHashToHex(hash_result.value());

        try {
            co_await m_redis_client->execCommandCoro(
                "SET %s %s EX %d",
                key.c_str(),
                hash.c_str(),
                GetRefreshTokenExpireSeconds()
            );

            LOG_DEBUG << "Refresh token 存储成功: user_id=" << user_id;
            co_return true;

        } catch (const drogon::nosql::RedisException& ex) {
            LOG_ERROR << "Redis 操作失败: " << ex.what();
            co_return false;
        }
    }

    auto TokenService::RefreshRefreshToken(uint64_t user_id, const std::string& old_token, const std::string& new_token) const
        -> drogon::Task<Result<void>> {

        const auto key = "refresh_token:" + std::to_string(user_id);

        auto old_hash_result = HashUtil::HashToken(old_token);
        if (!old_hash_result) {
            co_return std::unexpected(old_hash_result.error());
        }
        auto new_hash_result = HashUtil::HashToken(new_token);
        if (!new_hash_result) {
            co_return std::unexpected(new_hash_result.error());
        }
        const auto old_hash = HashUtil::TokenHashToHex(old_hash_result.value());
        const auto new_hash = HashUtil::TokenHashToHex(new_hash_result.value());

        try {
            auto result = co_await m_redis_client->execCommandCoro("GET %s", key.c_str());

            if (result.isNil()) {
                LOG_WARN << "Refresh token 不存在: user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::InvalidRefreshToken));
            }

            const auto current_hash = result.asString();
            if (current_hash != old_hash) {
                LOG_WARN << "Refresh token 已被使用: user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::RefreshTokenAlreadyUsed));
            }

            // 步骤 3: SET 新值
            co_await m_redis_client->execCommandCoro(
                "SET %s %s EX %d",
                key.c_str(),
                new_hash.c_str(),
                GetRefreshTokenExpireSeconds()
            );

            LOG_DEBUG << "Refresh token 更新成功: user_id=" << user_id;
            co_return {};

        } catch (const drogon::nosql::RedisException& ex) {
            LOG_ERROR << "Redis 操作失败: " << ex.what();
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Redis 操作失败"));
        }
    }

    auto TokenService::InvalidateAccessToken(const std::string& token) const -> drogon::Task<bool> {
        // 步骤 1: 提取 JTI
        auto jti_result = ExtractJti(token);
        if (!jti_result) {
            LOG_WARN << "提取 JTI 失败: " << jti_result.error().message;
            co_return false;
        }
        const auto& jti = jti_result.value();

        // 步骤 2: 计算剩余 TTL
        auto ttl_result = CalculateRemainingTtl(token);
        if (!ttl_result) {
            LOG_WARN << "计算 TTL 失败: " << ttl_result.error().message;
            co_return false;
        }
        const auto ttl = ttl_result.value();

        try {
            // 步骤 3: 存储到黑名单
            const auto key = "access_token_blacklist:" + jti;
            co_await m_redis_client->execCommandCoro(
                "SETEX %s %d %s",
                key.c_str(),
                ttl,
                "1"
            );

            LOG_INFO << "访问令牌已失效: jti=" << jti << ", ttl=" << ttl << "s";
            co_return true;
        } catch (const drogon::nosql::RedisException& ex) {
            LOG_ERROR << "Redis 操作失败: " << ex.what();
            co_return false;
        }
    }

    auto TokenService::RevokeRefreshToken(uint64_t user_id) const -> drogon::Task<bool> {
        const auto key = "refresh_token:" + std::to_string(user_id);

        try {
            auto result = co_await m_redis_client->execCommandCoro("DEL %s", key.c_str());
            const auto deleted = result.asInteger();

            if (deleted > 0) {
                LOG_INFO << "刷新令牌已撤销: user_id=" << user_id;
            }

            co_return true;
        } catch (const drogon::nosql::RedisException& ex) {
            LOG_ERROR << "Redis 操作失败: " << ex.what();
            co_return false;
        }
    }

    auto TokenService::IsAccessTokenRevoked(const std::string& jti) const -> drogon::Task<bool> {
        const auto key = "access_token_blacklist:" + jti;

        try {
            auto result = co_await m_redis_client->execCommandCoro("EXISTS %s", key.c_str());
            const auto exists = result.asInteger();

            co_return exists == 1;
        } catch (const drogon::nosql::RedisException& ex) {
            LOG_ERROR << "Redis 操作失败: " << ex.what();
            co_return false; // 容错：Redis 失败时允许通过
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

            auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
                                 exp - now
            )
                                 .count();

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
