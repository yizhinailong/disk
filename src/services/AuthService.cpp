/**
 * @file AuthService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 认证服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "AuthService.hpp"

#include <drogon/utils/coroutine.h>
#include <json/writer.h>

#include "dtos/AuthDto.hpp"
#include "models/OperationLogs.hpp"
#include "services/ObservedDbClient.hpp"
#include "services/TokenService.hpp"
#include "utils/AuthCpuPool.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/HashUtil.hpp"
#include "utils/RedisKeyPrefix.hpp"

namespace disk::auth {

    using disk::services::TokenService;
    using disk::utils::ConfigMgr;
    using disk::utils::HashUtil;
    using disk::utils::RunOnAuthCpuPool;
    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::Users;

    namespace {
        constexpr int16_t ACCOUNT_STATUS_DISABLED = 0;
        constexpr int16_t ACCOUNT_STATUS_ACTIVE = 1;
        constexpr int16_t ACCOUNT_STATUS_LOCKED = 2;
        constexpr int LOGIN_FAILURE_LIMIT = 5;

        [[nodiscard]] auto UserToResponse(const Users& user) -> RegisterResponse {
            RegisterResponse response;
            response.id = user.getValueOfId();
            response.username = user.getValueOfUsername();
            response.email = user.getValueOfEmail();
            response.nickname = user.getNickname() ? *user.getNickname() : user.getValueOfUsername();
            response.storage_quota = user.getValueOfStorageQuota();
            response.storage_used = user.getValueOfStorageUsed();
            response.created_at = user.getValueOfCreatedAt().toDbStringLocal();
            return response;
        }
    } // namespace

    AuthService::AuthService(const drogon::nosql::RedisClientPtr& redis_client)
        : m_db_client(disk::metrics::ObserveDbClient(drogon::app().getDbClient())),
          m_redis_service(disk::services::RedisService::GetInstance()) {
        /// 如果尚未初始化，则初始化 RedisService 单例
        disk::services::RedisService::Initialize(redis_client);
        /// 初始化 TokenService 单例
        disk::services::TokenService::Initialize(ConfigMgr::GetInstance()->GetJwtSecret());

        Logger::Debug(disk::utils::ServiceRuntimeLogContext()) << "Service initialized: service=auth";
    }

    auto AuthService::Register(
        RegisterRequest request,
        disk::utils::LogContext log_context
    ) -> drogon::Task<Result<RegisterResponse>> {
        Logger::Debug(log_context) << "User registration started";

        /// 1. 检查用户名和邮箱是否已存在（单次查询）
        try {
            auto result = co_await m_db_client->execSqlCoro(
                "SELECT " "  COUNT(CASE WHEN username = $1 THEN 1 END) AS username_count, " "  COUNT(CASE WHEN email = $2 THEN 1 END) AS email_count " "FROM users",
                request.username,
                request.email
            );

            if (!result.empty()) {
                auto username_count = result[0]["username_count"].as<int>();
                auto email_count = result[0]["email_count"].as<int>();

                if (username_count > 0) {
                    Logger::Warn(log_context) << "Registration username already exists";
                    co_return std::unexpected(ErrorInfo(ErrorCode::UsernameExists));
                }

                if (email_count > 0) {
                    Logger::Warn(log_context) << "Registration email already exists";
                    co_return std::unexpected(ErrorInfo(ErrorCode::EmailExists));
                }
            }
        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Error(log_context) << "Uniqueness check failed";
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Registration failed, please try again later")
            );
        }

        /// 2. 加密密码（使用 libsodium Argon2id）
        Logger::Debug(log_context) << "Registration password hash started";
        auto hash_result = co_await RunOnAuthCpuPool(
            [password = std::move(request.password)]() {
                return HashUtil::HashPassword(password);
            }
        );
        if (!hash_result) {
            Logger::Error(log_context) << "Registration password hash failed";
            co_return std::unexpected(hash_result.error());
        }
        Logger::Debug(log_context) << "Registration password hash completed";

        /// 3. 创建用户记录
        Users user;
        user.setUsername(request.username);
        user.setEmail(request.email);
        user.setPasswordHash(hash_result.value());
        user.setNickname(request.username); ///< 默认昵称为用户名
        user.setStorageQuota(DEFAULT_STORAGE_QUOTA);
        user.setStorageUsed(0);
        user.setStatus(1); ///< 正常状态
        user.setLoginAttempts(0);

        /// 4. 插入数据库
        try {
            CoroMapper<Users> mapper(m_db_client);
            user = co_await mapper.insert(user);
            Logger::Info(log_context) << "User registration data inserted";
        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Error(log_context) << "User registration database insert failed";
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Registration failed, please try again later")
            );
        }

        /// 5. 返回用户信息
        auto response = UserToResponse(user);
        Logger::Info(log_context) << "User registration completed";
        co_return response;
    }

    auto AuthService::Login(
        LoginRequest request,
        std::string ip_address,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<LoginResponse>> {

        Logger::Debug(log_context) << "User login started";
        const std::string client_ip =
            disk::redis::RedisKeyPrefix::ExtractIPOnly(ip_address);

        /// 0. 检查 IP 登录频率限制
        const std::string rate_key =
            disk::redis::RedisKeyPrefix::BuildLoginRateLimitKey(client_ip);

        /// 使用 Lua 脚本原子递增计数并设置过期时间（单次 Redis 交互）
        auto incr_result = co_await m_redis_service->IncrWithExpire(rate_key, 300, log_context);
        if (incr_result.has_value()) {
            const auto count = incr_result.value();

            /// 检查是否超过阈值（5 次）
            if (count > 5) {
                Logger::Warn(log_context) << "Login rate limit exceeded";
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::TooManyRequests,
                    "Too many login attempts, please try again in 5 minutes"
                ));
            }
        } else {
            /// 失败开放策略：Redis 失败时只记录警告，不阻止登录
            Logger::Warn(log_context) << "Redis rate limit check failed";
        }

        /// 1. 查找用户（用户名或邮箱）
        auto user_result = co_await FindUser(request.account, log_context);
        if (!user_result) {
            Logger::Warn(log_context) << "Login account not found";
            co_return std::unexpected(user_result.error());
        }

        const auto& user = *user_result;

        /// 2. 使用数据库时间检查账户状态
        auto access_result =
            co_await ValidateAccountAccess(user.getValueOfId(), log_context);
        if (!access_result) {
            co_return std::unexpected(access_result.error());
        }

        /// 3. 验证密码
        const auto stored_password_hash = user.getValueOfPasswordHash();
        auto password_matches = co_await RunOnAuthCpuPool(
            [password = request.password, stored_password_hash]() {
                return HashUtil::VerifyPassword(password, stored_password_hash);
            }
        );
        if (!password_matches) {
            Logger::Warn(log_context) << "Login password rejected";
            auto increment_result =
                co_await IncrementLoginAttempts(user.getValueOfId(), log_context);
            if (!increment_result) {
                co_return std::unexpected(increment_result.error());
            }
            co_return std::unexpected(ErrorInfo(ErrorCode::InvalidCredentials));
        }

        /// 4. 在签发令牌前原子更新登录状态
        auto update_result =
            co_await UpdateLoginInfo(user.getValueOfId(), client_ip, log_context);
        if (!update_result) {
            co_return std::unexpected(update_result.error());
        }

        /// 5. 生成令牌
        auto [access_token, refresh_token] = TokenService::GetInstance()->GenerateTokens(
            user.getValueOfId(),
            user.getValueOfUsername(),
            user.getValueOfRole(),
            ACCOUNT_STATUS_ACTIVE,
            log_context
        );

        /// 6. 存储 refresh_token 到 Redis
        auto store_result = co_await TokenService::GetInstance()->StoreRefreshToken(
            user.getValueOfId(),
            refresh_token,
            log_context
        );
        if (!store_result.has_value()) {
            Logger::Warn(log_context) << "Failed to store refresh token";
        }

        /// 7. 构造响应
        LoginResponse response;
        response.access_token = access_token;
        response.refresh_token = refresh_token;
        response.token_type = "Bearer";
        response.expires_in = disk::services::TokenService::GetAccessTokenExpireSeconds();
        response.user = UserToResponse(user);

        Logger::Info(log_context) << "User login successful";
        co_return response;
    }

    auto AuthService::RefreshTokens(
        RefreshTokenRequest request,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<RefreshTokenResponse>> {
        Logger::Debug(log_context) << "Starting token refresh";

        /// 1. 验证刷新令牌
        auto verify_result = TokenService::GetInstance()->VerifyRefreshToken(
            request.refresh_token,
            log_context
        );
        if (!verify_result) {
            Logger::Warn(log_context) << "Refresh token verification failed";
            co_return std::unexpected(verify_result.error());
        }

        const auto [user_id, jti] = verify_result.value();
        Logger::Debug(log_context) << "Refresh token verified";

        /// 2. 查询用户信息
        try {
            CoroMapper<Users> mapper(m_db_client);

            auto user =
                co_await mapper.findOne(Criteria(Users::Cols::_id, CompareOperator::EQ, user_id));
            Logger::Debug(log_context) << "Refresh user loaded";

            /// 3. 使用数据库时间检查账户状态
            auto access_result = co_await ValidateAccountAccess(user_id, log_context);
            if (!access_result) {
                co_return std::unexpected(access_result.error());
            }

            /// 4. 生成新的令牌对
            auto [access_token, new_refresh_token] = TokenService::GetInstance()->GenerateTokens(
                user.getValueOfId(),
                user.getValueOfUsername(),
                user.getValueOfRole(),
                ACCOUNT_STATUS_ACTIVE,
                log_context
            );

            /// 5. 刷新 Redis 中的 token（原子操作）
            auto refresh_result = co_await TokenService::GetInstance()->RefreshRefreshToken(
                user.getValueOfId(),
                request.refresh_token,
                new_refresh_token,
                log_context
            );
            if (!refresh_result) {
                Logger::Warn(log_context) << "Refresh token renewal failed";
                co_return std::unexpected(refresh_result.error());
            }

            /// 7. 构造响应
            RefreshTokenResponse response;
            response.access_token = access_token;
            response.refresh_token = new_refresh_token;
            response.expires_in = disk::services::TokenService::GetAccessTokenExpireSeconds();

            Logger::Info(log_context) << "Token refresh successful";
            co_return response;

        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Error(log_context) << "User query failed";
            co_return std::unexpected(ErrorInfo(ErrorCode::UserNotFound));
        } catch (const std::exception&) {
            Logger::Error(log_context) << "Token refresh processing failed";
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Token refresh failed, please try again later")
            );
        }
    }

    auto
    AuthService::Logout(
        uint64_t user_id,
        const std::string& access_token,
        std::string ip_address,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<void>> {

        const std::string client_ip =
            disk::redis::RedisKeyPrefix::ExtractIPOnly(ip_address);
        Logger::Info(log_context) << "User logout started";

        /// 步骤 1: 使访问令牌失效
        auto invalidate_result = co_await TokenService::GetInstance()->InvalidateAccessToken(
            access_token,
            log_context
        );
        if (!invalidate_result.has_value()) {
            Logger::Warn(log_context) << "Access token invalidation failed";
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Logout failed, please try again later")
            );
        }

        /// 步骤 2: 撤销刷新令牌
        auto revoke_result =
            co_await TokenService::GetInstance()->RevokeRefreshToken(user_id, log_context);
        if (!revoke_result) {
            Logger::Warn(log_context) << "Refresh token revocation failed";
            /// 不中断流程，继续返回成功
        }

        /// 步骤 3: 记录登出日志到 operation_logs
        try {
            drogon::orm::CoroMapper<drogon_model::disk::OperationLogs> mapper(m_db_client);

            drogon_model::disk::OperationLogs log;
            log.setUserId(user_id);
            log.setAction("logout");
            log.setTargetType("user");
            log.setTargetId(0);
            Json::Value details_json;
            details_json["message"] = "User logged out";
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            log.setDetails(Json::writeString(builder, details_json));
            log.setIpAddress(client_ip);
            log.setCreatedAt(trantor::Date::now());

            co_await mapper.insert(log);
            Logger::Debug(log_context) << "Logout audit recorded";
        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Warn(log_context) << "Failed to record logout log";
            /// 不中断流程
        }

        Logger::Info(log_context) << "User logout successful";
        co_return {};
    }

    auto AuthService::FindUser(
        std::string account,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<Result<Users>> {
        try {
            auto result = co_await m_db_client->execSqlCoro(
                "SELECT id, username, email, password_hash, nickname, avatar, " "storage_quota, storage_used, storage_reserved, status, role, " "login_attempts, locked_until, last_login_at, last_login_ip, " "created_at, updated_at " "FROM users WHERE username = $1 OR email = $1 LIMIT 1",
                account
            );

            if (result.empty()) {
                Logger::Warn(log_context) << "User lookup found no match";
                co_return std::unexpected(ErrorInfo(ErrorCode::UserNotFound));
            }

            Users user(result[0], -1);
            Logger::Debug(log_context) << "User lookup succeeded";
            co_return user;

        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Warn(log_context) << "User lookup failed";
            co_return std::unexpected(ErrorInfo(ErrorCode::UserNotFound));
        }
    }

    auto AuthService::ValidateAccountAccess(
        uint64_t user_id,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<Result<void>> {
        try {
            auto result = co_await m_db_client->execSqlCoro(
                "SELECT status, ((status = 2 AND locked_until IS NULL) " "OR COALESCE(locked_until > NOW(), FALSE)) AS account_locked " "FROM users WHERE id = $1",
                user_id
            );

            if (result.empty()) {
                co_return std::unexpected(ErrorInfo(ErrorCode::UserNotFound));
            }

            if (result[0]["status"].as<int>() == ACCOUNT_STATUS_DISABLED) {
                Logger::Warn(log_context) << "Account disabled: user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::AccountDisabled));
            }

            if (result[0]["account_locked"].as<bool>()) {
                Logger::Warn(log_context) << "Account locked: user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::AccountLocked));
            }

            co_return {};
        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Error(log_context) << "Failed to validate account access";
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to validate account status")
            );
        }
    }

    auto AuthService::UpdateLoginInfo(
        uint64_t user_id,
        std::string client_ip,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<void>> {

        try {
            auto result = co_await m_db_client->execSqlCoro(
                "UPDATE users SET status = $1, login_attempts = 0, locked_until = NULL, " "last_login_at = NOW(), last_login_ip = $2, updated_at = NOW() " "WHERE id = $3 AND ((status = $1 AND " "(locked_until IS NULL OR locked_until <= NOW())) OR " "(status = $4 AND locked_until IS NOT NULL AND locked_until <= NOW())) " "RETURNING id",
                ACCOUNT_STATUS_ACTIVE,
                client_ip,
                user_id,
                ACCOUNT_STATUS_LOCKED
            );

            if (result.empty()) {
                auto access_result = co_await ValidateAccountAccess(user_id, log_context);
                if (!access_result) {
                    co_return std::unexpected(access_result.error());
                }

                Logger::Error(log_context)
                    << "Login state changed before update: user_id=" << user_id;
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to update login state")
                );
            }

            Logger::Debug(log_context) << "Login info updated successfully: " << user_id;

            /// 清除 IP 频率限制计数器
            const std::string rate_key =
                disk::redis::RedisKeyPrefix::BuildLoginRateLimitKey(client_ip);

            auto delete_result = co_await m_redis_service->Delete(rate_key, log_context);
            if (delete_result.has_value()) {
                Logger::Debug(log_context)
                    << "Login rate limit counter cleared: ip=" << client_ip;
            } else {
                Logger::Warn(log_context) << "Failed to clear login rate limit counter";
            }
        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Error(log_context) << "Failed to update login info";
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to update login state")
            );
        }

        co_return {};
    }

    auto AuthService::IncrementLoginAttempts(
        uint64_t user_id,
        disk::utils::LogContext log_context
    ) -> drogon::Task<Result<void>> {

        try {
            auto result = co_await m_db_client->execSqlCoro(
                "UPDATE users SET status = $1, " "login_attempts = CASE " "WHEN locked_until IS NOT NULL AND locked_until <= NOW() THEN 1 " "ELSE login_attempts + 1 END, " "locked_until = CASE WHEN (CASE " "WHEN locked_until IS NOT NULL AND locked_until <= NOW() THEN 1 " "ELSE login_attempts + 1 END) >= $2 " "THEN NOW() + INTERVAL '15 minutes' ELSE NULL END, updated_at = NOW() " "WHERE id = $3 AND ((status = $1 AND " "(locked_until IS NULL OR locked_until <= NOW())) OR " "(status = $4 AND locked_until IS NOT NULL AND locked_until <= NOW())) " "RETURNING login_attempts, locked_until > NOW() AS account_locked",
                ACCOUNT_STATUS_ACTIVE,
                LOGIN_FAILURE_LIMIT,
                user_id,
                ACCOUNT_STATUS_LOCKED
            );

            if (result.empty()) {
                Logger::Debug(log_context)
                    << "Login attempt not counted for unavailable account: user_id=" << user_id;
                co_return {};
            }

            const auto attempts = result[0]["login_attempts"].as<int>();
            if (result[0]["account_locked"].as<bool>()) {
                Logger::Warn(log_context)
                    << "Account locked: " << user_id << " (unlocks in 15 minutes)";
            } else {
                Logger::Warn(log_context)
                    << "Failed login attempts: " << user_id << " = " << attempts;
            }
        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Error(log_context) << "Failed to increment login attempts";
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to record login attempt")
            );
        }

        co_return {};
    }
} // namespace disk::auth
