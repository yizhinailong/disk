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
        : m_redis_client(std::move(redis_client)) {}

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

} // namespace disk::services
