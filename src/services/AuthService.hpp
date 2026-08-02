/**
 * @file AuthService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 认证服务
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <string>

#include <drogon/orm/DbClient.h>

#include "dtos/AuthDto.hpp"
#include "models/Users.hpp"
#include "services/RedisService.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/LogHelper.hpp"

namespace disk::auth {

    /**
     * @brief 认证服务类
     */
    class AuthService {
    public:
        /**
         * @brief 构造函数
         * @param redis_client Redis客户端（数据库客户端从全局获取）
         */
        explicit AuthService(const drogon::nosql::RedisClientPtr& redis_client);
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
         * @param log_context 请求日志上下文
         * @return drogon::Task<Result<RegisterResponse>> 成功返回用户信息，失败返回错误
         */
        [[nodiscard]]
        auto Register(
            RegisterRequest request,
            disk::utils::LogContext log_context = {}
        ) -> drogon::Task<Result<RegisterResponse>>;

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
         * @param ip_address 连接 peer 端点（可包含 TCP 源端口）
         * @param log_context 请求日志上下文
         * @return drogon::Task<Result<LoginResponse>> 成功返回令牌和用户信息，失败返回错误
         */
        [[nodiscard]]
        auto Login(
            LoginRequest request,
            std::string ip_address,
            disk::utils::LogContext log_context = {}
        ) -> drogon::Task<Result<LoginResponse>>;

        /**
         * @brief 刷新令牌
         *
         * 业务规则：
         * - 验证刷新令牌的有效性
         * - 检查用户账户状态（禁用、锁定）
         * - 生成新的令牌对（access_token + refresh_token）
         * - 旧 refresh token 会在 Redis 中被新 token 覆盖
         *
         * @param request 刷新令牌请求
         * @param log_context 请求日志上下文
         * @return drogon::Task<Result<RefreshTokenResponse>> 成功返回新令牌对，失败返回错误
         */
        [[nodiscard]]
        auto RefreshTokens(
            RefreshTokenRequest request,
            disk::utils::LogContext log_context = {}
        ) -> drogon::Task<Result<RefreshTokenResponse>>;

        /**
         * @brief 用户登出
         *
         * 业务规则：
         * - 使访问令牌失效（加入黑名单）
         * - 撤销刷新令牌
         * - 记录登出日志到 operation_logs 表
         *
         * @param user_id 用户 ID
         * @param access_token 访问令牌
         * @param ip_address 连接 peer 端点（可包含 TCP 源端口）
         * @param log_context 请求日志上下文
         * @return drogon::Task<Result<void>> 成功返回 void，失败返回错误
         */
        [[nodiscard]]
        auto Logout(
            uint64_t user_id,
            const std::string& access_token,
            std::string ip_address,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<void>>;

    private:
        /**
         * @brief 根据账号（用户名或邮箱）查找用户
         * @param account 账号（用户名或邮箱）
         * @param log_context 请求日志上下文
         * @return drogon::Task<Result<drogon_model::disk::Users>> 成功返回用户信息，失败返回 UserNotFound 错误
         */
        [[nodiscard]]
        auto FindUser(
            std::string account,
            disk::utils::LogContext log_context
        ) const -> drogon::Task<Result<drogon_model::disk::Users>>;

        /**
         * @brief 使用 PostgreSQL 时间校验账户是否允许认证
         * @param user_id 用户ID
         * @param log_context 请求日志上下文
         * @return 允许认证时成功，否则返回禁用、锁定或数据库错误
         */
        [[nodiscard]]
        auto ValidateAccountAccess(
            uint64_t user_id,
            disk::utils::LogContext log_context
        ) const -> drogon::Task<Result<void>>;

        /**
         * @brief 更新登录信息
         * @param user_id 用户ID
         * @param client_ip 已归一化且不含端口的客户端 IP 地址
         * @param log_context 请求日志上下文
         * @return 登录状态原子写入成功时返回 void
         */
        [[nodiscard]]
        auto UpdateLoginInfo(
            uint64_t user_id,
            std::string client_ip,
            disk::utils::LogContext log_context
        ) -> drogon::Task<Result<void>>;

        /**
         * @brief 增加登录失败次数
         * @param user_id 用户ID
         * @param log_context 请求日志上下文
         * @return 失败计数原子写入成功或账户已转为不可用时返回 void
         */
        [[nodiscard]]
        auto IncrementLoginAttempts(
            uint64_t user_id,
            disk::utils::LogContext log_context
        ) -> drogon::Task<Result<void>>;

        drogon::orm::DbClientPtr m_db_client;                            ///< 数据库客户端
        std::shared_ptr<disk::services::RedisService> m_redis_service{}; ///< Redis服务
        static constexpr uint64_t DEFAULT_STORAGE_QUOTA = 10737418240;   ///< 默认存储配额 10GB
    };

} // namespace disk::auth
