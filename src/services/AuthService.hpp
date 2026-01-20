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

#include "dtos/AuthDto.hpp"
#include "models/Users.hpp"
#include "services/TokenService.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::auth {

    /**
     * @brief 认证服务类
     */
    class AuthService {
    public:
        explicit AuthService();
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
         * @return drogon::Task<Result<RegisterResponse>> 成功返回用户信息，失败返回错误
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
         * @return drogon::Task<Result<LoginResponse>> 成功返回令牌和用户信息，失败返回错误
         */
        [[nodiscard]]
        auto Login(LoginRequest request, std::string ip_address) -> drogon::Task<Result<LoginResponse>>;

        /**
         * @brief 刷新令牌
         *
         * 业务规则：
         * - 验证刷新令牌的有效性
         * - 检查用户账户状态（禁用、锁定）
         * - 生成新的令牌对（access_token + refresh_token）
         * - TODO: 将旧 refresh token 加入黑名单（Redis）
         *
         * @param request 刷新令牌请求
         * @return drogon::Task<Result<RefreshTokenResponse>> 成功返回新令牌对，失败返回错误
         */
        [[nodiscard]]
        auto RefreshTokens(RefreshTokenRequest request) -> drogon::Task<Result<RefreshTokenResponse>>;

    private:
        /**
         * @brief 检查用户名是否已存在
         * @param username 用户名
         * @return drogon::Task<bool> 用户名是否存在
         */
        [[nodiscard]]
        auto IsUsernameExists(std::string username) const -> drogon::Task<bool>;

        /**
         * @brief 检查邮箱是否已存在
         * @param email 邮箱地址
         * @return drogon::Task<bool> 邮箱是否已存在
         */
        [[nodiscard]]
        auto IsEmailExists(std::string email) const -> drogon::Task<bool>;

        /**
         * @brief 用户模型转响应结构
         * @param user 用户模型
         * @return RegisterResponse 响应结构
         */
        [[nodiscard]]
        static auto UserToResponse(const drogon_model::disk::Users& user) -> RegisterResponse;

        /**
         * @brief 根据账号（用户名或邮箱）查找用户
         * @param account 账号（用户名或邮箱）
         * @return drogon::Task<Result<drogon_model::disk::Users>> 成功返回用户信息，失败返回 UserNotFound 错误
         */
        [[nodiscard]]
        auto FindUser(std::string account) const -> drogon::Task<Result<drogon_model::disk::Users>>;

        /**
         * @brief 检查账户是否被锁定
         * @param user 用户模型
         * @return bool 账户是否被锁定
         */
        [[nodiscard]]
        auto CheckAccountLocked(const drogon_model::disk::Users& user) const -> bool;

        /**
         * @brief 更新登录信息
         * @param user_id 用户ID
         * @param ip_address IP地址
         * @return drogon::Task<void>
         */
        auto UpdateLoginInfo(uint64_t user_id, std::string ip_address) -> drogon::Task<void>;

        /**
         * @brief 增加登录失败次数
         * @param user_id 用户ID
         * @return drogon::Task<void>
         */
        auto IncrementLoginAttempts(uint64_t user_id) -> drogon::Task<void>;

        drogon::orm::DbClientPtr m_db_client;                          ///< 数据库客户端
        std::shared_ptr<TokenService> m_token_service;                 ///< JWT令牌服务
        static constexpr uint64_t DEFAULT_STORAGE_QUOTA = 10737418240; ///< 默认存储配额 10GB
    };

} // namespace disk::auth
