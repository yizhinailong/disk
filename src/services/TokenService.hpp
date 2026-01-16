/**
 * @file TokenService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief JWT令牌服务（最小化实现）
 * @version 0.1
 * @date 2026-01-16
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <string>
#include <utility>

namespace disk::auth {

    /**
     * @brief JWT令牌服务
     *
     * 最小化实现：
     * - 仅支持生成 Access Token 和 Refresh Token
     * - 不实现验证、黑名单等功能（后续需要时添加）
     */
    class TokenService {
    public:
        /**
         * @brief 构造函数
         * @param jwt_secret JWT签名密钥
         */
        explicit TokenService(std::string jwt_secret);

        /**
         * @brief 生成令牌对
         * @param user_id 用户ID
         * @param username 用户名
         * @return pair<access_token, refresh_token>
         */
        [[nodiscard]]
        auto GenerateTokens(uint64_t user_id, const std::string& username) const -> std::pair<std::string, std::string>;

        /**
         * @brief 获取访问令牌过期时间（秒）
         */
        [[nodiscard]]
        static constexpr auto GetAccessTokenExpireSeconds() noexcept -> int {
            return 7200;
        }

        /**
         * @brief 获取刷新令牌过期时间（秒）
         */
        [[nodiscard]]
        static constexpr auto GetRefreshTokenExpireSeconds() noexcept -> int {
            return 604800;
        }

    private:
        std::string m_jwt_secret;
    };

} // namespace disk::auth
