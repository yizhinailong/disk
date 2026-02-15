/**
 * @file ShareTokenService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 分享令牌服务
 * @version 0.1
 * @date 2026-02-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <string>

#include <drogon/nosql/RedisClient.h>

#include "services/RedisService.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::services {

    struct ShareTokenClaims {
        std::string share_code;
        uint64_t share_id;
        std::string jti;
    };

    class ShareTokenService {
    public:
        explicit ShareTokenService(std::string jwt_secret, drogon::nosql::RedisClientPtr redis_client);
        explicit ShareTokenService(std::string jwt_secret, std::shared_ptr<RedisService> redis_service);

        [[nodiscard]]
        static auto GenerateToken(const std::string& jwt_secret, const std::string& share_code, uint64_t share_id)
            -> Result<std::string>;

        [[nodiscard]]
        static auto VerifyToken(const std::string& jwt_secret, const std::string& token)
            -> Result<ShareTokenClaims>;

        [[nodiscard]]
        static auto ExtractTokenHash(const std::string& token) -> Result<std::string>;

        [[nodiscard]]
        static constexpr auto GetShareTokenExpireSeconds() noexcept -> int {
            return 3600;
        }

        [[nodiscard]]
        auto StoreShareToken(const std::string& share_code, const std::string& token)
            -> drogon::Task<Result<void>>;

        [[nodiscard]]
        auto VerifyShareToken(const std::string& share_code, const std::string& token)
            -> drogon::Task<Result<ShareTokenClaims>>;

        [[nodiscard]]
        auto RevokeShareToken(const std::string& token) -> drogon::Task<Result<void>>;

        [[nodiscard]]
        auto IsShareTokenRevoked(const std::string& token_hash) -> drogon::Task<bool>;

    private:
        std::string m_jwt_secret;
        drogon::nosql::RedisClientPtr m_redis_client;
        std::shared_ptr<RedisService> m_redis_service;
    };

} // namespace disk::services
