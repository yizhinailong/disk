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
         * @brief 加密密码（使用 libsodium Argon2id 算法）
         */
        [[nodiscard]]
        static auto HashPassword(const std::string& password) -> std::string;

        /**
         * @brief 验证密码
         * @param password 明文密码
         * @param hash 存储的哈希值
         * @return 密码是否匹配
         */
        [[nodiscard]]
        static auto VerifyPassword(const std::string& password, const std::string& hash) -> bool;

        /**
         * @brief 用户模型转响应结构
         */
        [[nodiscard]]
        static auto UserToResponse(const drogon_model::disk::Users& user) -> RegisterResponse;

        drogon::orm::DbClientPtr m_db_client;                          ///< 数据库客户端
        static constexpr uint64_t DEFAULT_STORAGE_QUOTA = 10737418240; ///< 默认存储配额 10GB
    };

} // namespace disk::auth
