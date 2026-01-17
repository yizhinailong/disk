/**
 * @file AuthService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 认证服务
 * @version 0.1
 * @date 2026-01-14
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <string>

#include <drogon/orm/DbClient.h>

#include "models/Users.hpp"
#include "requests/AuthRequest.hpp"
#include "services/TokenService.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::auth {

    /**
     * @brief 用户注册响应结构
     */
    struct RegisterResponse {
        uint64_t id;
        std::string username;
        std::string email;
        std::string nickname;
        uint64_t storage_quota;
        std::string created_at;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["id"] = static_cast<Json::UInt64>(id);
            json["username"] = username;
            json["email"] = email;
            json["nickname"] = nickname;
            json["storage_quota"] = static_cast<Json::UInt64>(storage_quota);
            json["created_at"] = created_at;
            return json;
        }
    };

    /**
     * @brief 用户登录响应结构
     */
    struct LoginResponse {
        std::string access_token;
        std::string refresh_token;
        std::string token_type;
        int expires_in;
        RegisterResponse user;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["access_token"] = access_token;
            json["refresh_token"] = refresh_token;
            json["token_type"] = token_type;
            json["expires_in"] = expires_in;
            json["user"] = user.ToJson();
            return json;
        }
    };

    /**
     * @brief 认证服务类
     */
    class AuthService {
    public:
        explicit AuthService(drogon::orm::DbClientPtr db_client);
        ~AuthService() = default;
        AuthService(const AuthService&) = delete;
        auto operator=(const AuthService&) -> AuthService& = delete;
        AuthService(AuthService&&) = default;
        auto operator=(AuthService&&) -> AuthService& = default;

        /**
         * @brief 用户注册
         *
         * 业务规则：
         * - 验证用户名和邮箱的唯一性
         * - 密码使用 libsodium 的 Argon2id 算法加密存储
         * - 分配默认存储配额（10GB）
         *
         * @param request 注册请求
         * @return Result<RegisterResponse> 成功返回用户信息，失败返回错误
         */
        [[nodiscard]]
        auto Register(RegisterRequest request) -> drogon::Task<Result<RegisterResponse>>;

        /**
         * @brief 用户登录
         *
         * 业务规则：
         * - 支持用户名或邮箱登录
         * - 密码错误5次锁定账户15分钟
         * - 检查账户禁用/锁定状态
         * - 登录成功后更新登录信息
         *
         * @param request 登录请求
         * @param ip_address 客户端IP地址
         * @return Result<LoginResponse> 成功返回令牌和用户信息，失败返回错误
         */
        [[nodiscard]]
        auto Login(LoginRequest request, std::string ip_address) -> drogon::Task<Result<LoginResponse>>;

        /**
         * @brief 从配置读取 JWT 密钥
         */
        [[nodiscard]]
        static auto GetJwtSecret() -> std::string;

    private:
        /**
         * @brief 检查用户名是否已存在
         */
        [[nodiscard]]
        auto IsUsernameExists(std::string username) const -> drogon::Task<bool>;

        /**
         * @brief 检查邮箱是否已存在
         */
        [[nodiscard]]
        auto IsEmailExists(std::string email) const -> drogon::Task<bool>;

        /**
         * @brief 用户模型转响应结构
         */
        [[nodiscard]]
        static auto UserToResponse(const drogon_model::disk::Users& user) -> RegisterResponse;

        /**
         * @brief 根据账号（用户名或邮箱）查找用户
         */
        [[nodiscard]]
        auto FindUser(std::string account) const -> drogon::Task<std::optional<drogon_model::disk::Users>>;

        /**
         * @brief 检查账户是否被锁定
         */
        [[nodiscard]]
        auto CheckAccountLocked(const drogon_model::disk::Users& user) const -> bool;

        /**
         * @brief 更新登录信息
         */
        auto UpdateLoginInfo(uint64_t user_id, std::string ip_address) -> drogon::Task<void>;

        /**
         * @brief 增加登录失败次数
         */
        auto IncrementLoginAttempts(uint64_t user_id) -> drogon::Task<void>;

        drogon::orm::DbClientPtr m_db_client;                          ///< 数据库客户端
        TokenService m_token_service;                                  ///< JWT令牌服务
        static constexpr uint64_t DEFAULT_STORAGE_QUOTA = 10737418240; ///< 默认存储配额 10GB
    };

} // namespace disk::auth
