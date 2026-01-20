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

#include "utils/TokenHash.hpp"

namespace disk::services {

    using ::ErrorCode;
    using disk::error::ErrorInfo;

    RedisService::RedisService(drogon::nosql::RedisClientPtr redis_client)
        : m_redis_client(std::move(redis_client)) {}

    auto RedisService::StoreRefreshToken(uint64_t user_id, const std::string& refresh_token)
        -> drogon::Task<Result<void>> {

        const auto key = "refresh_token:" + std::to_string(user_id);
        const auto hash = TokenHashUtils::ToHex(TokenHashUtils::Hash(refresh_token));

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
        const auto old_hash = TokenHashUtils::ToHex(TokenHashUtils::Hash(old_token));
        const auto new_hash = TokenHashUtils::ToHex(TokenHashUtils::Hash(new_token));

        try {
            // 步骤 1: GET 当前值
            auto result = co_await m_redis_client->execCommandCoro("GET %s", key.c_str());

            if (result.isNil()) {
                LOG_WARN << "Refresh token 不存在: user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::InvalidRefreshToken));
            }

            // 步骤 2: 验证旧 token
            const auto current_hash = result.asString();
            if (current_hash != old_hash) {
                LOG_WARN << "Refresh token 已被使用或已刷新: user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::RefreshTokenAlreadyUsed));
            }

            // 步骤 3: SET 新值
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

} // namespace disk::services
