/**
 * @file RedisService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Redis 服务实现
 * @version 0.1
 * @date 2026-01-21
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "RedisService.hpp"

#include "utils/ErrorCode.hpp"
#include "utils/HashUtil.hpp"

namespace disk::services {

    using disk::error::ErrorInfo;
    using disk::utils::HashUtil;

    RedisService::RedisService(drogon::nosql::RedisClientPtr redis_client)
        : m_redis_client(std::move(redis_client)) {
        LOG_DEBUG << "RedisService 初始化成功";
    }

    auto RedisService::StoreRefreshToken(uint64_t user_id, const std::string& refresh_token)
        -> drogon::Task<Result<void>> {

        const auto key = "refresh_token:" + std::to_string(user_id);

        auto hash_result = HashUtil::HashToken(refresh_token);
        if (!hash_result) {
            co_return std::unexpected(hash_result.error());
        }
        const auto hash = HashUtil::TokenHashToHex(hash_result.value());

        try {
            co_await m_redis_client->execCommandCoro(
                "SET %s %s EX %d",
                key.c_str(),
                hash.c_str(),
                REFRESH_TOKEN_TTL
            );

            LOG_DEBUG << "Refresh token 存储成功: user_id=" << user_id;
            co_return {};

        } catch (const drogon::nosql::RedisException& ex) {
            LOG_ERROR << "Redis 操作失败: " << ex.what();
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Redis 操作失败"));
        }
    }

    auto RedisService::RefreshRefreshToken(
        uint64_t user_id,
        const std::string& old_token,
        const std::string& new_token
    ) -> drogon::Task<Result<void>> {

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
                LOG_WARN << "Refresh token 已被使用或已刷新: user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::RefreshTokenAlreadyUsed));
            }

            co_await m_redis_client->execCommandCoro(
                "SET %s %s EX %d",
                key.c_str(),
                new_hash.c_str(),
                REFRESH_TOKEN_TTL
            );

            LOG_DEBUG << "Refresh token 更新成功: user_id=" << user_id;
            co_return {};

        } catch (const drogon::nosql::RedisException& ex) {
            LOG_ERROR << "Redis 操作失败: " << ex.what();
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Redis 操作失败"));
        }
    }

    auto RedisService::InvalidateAccessToken(const std::string& token) -> drogon::Task<Result<void>> {
        auto jti_result = HashUtil::HashToken(token);
        if (!jti_result) {
            co_return std::unexpected(jti_result.error());
        }
        const auto jti = HashUtil::TokenHashToHex(jti_result.value());

        try {
            const auto key = "access_token_blacklist:" + jti;
            co_await m_redis_client->execCommandCoro(
                "SETEX %s %d %s",
                key.c_str(),
                ACCESS_TOKEN_TTL,
                "1"
            );

            LOG_INFO << "访问令牌已失效: jti=" << jti << ", ttl=" << ACCESS_TOKEN_TTL << "s";
            co_return {};
        } catch (const drogon::nosql::RedisException& ex) {
            LOG_ERROR << "Redis 操作失败: " << ex.what();
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Redis 操作失败"));
        }
    }

    auto RedisService::RevokeRefreshToken(uint64_t user_id) -> drogon::Task<Result<void>> {
        const auto key = "refresh_token:" + std::to_string(user_id);

        try {
            auto result = co_await m_redis_client->execCommandCoro("DEL %s", key.c_str());
            const auto deleted = result.asInteger();

            if (deleted > 0) {
                LOG_INFO << "刷新令牌已撤销: user_id=" << user_id;
            }

            co_return {};
        } catch (const drogon::nosql::RedisException& ex) {
            LOG_ERROR << "Redis 操作失败: " << ex.what();
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Redis 操作失败"));
        }
    }

    auto RedisService::IsAccessTokenRevoked(const std::string& jti) -> drogon::Task<bool> {
        const auto key = "access_token_blacklist:" + jti;

        try {
            auto result = co_await m_redis_client->execCommandCoro("EXISTS %s", key.c_str());
            const auto exists = result.asInteger();

            co_return exists == 1;
        } catch (const drogon::nosql::RedisException& ex) {
            LOG_ERROR << "Redis 操作失败: " << ex.what();
            co_return false;
        }
    }

} // namespace disk::services
