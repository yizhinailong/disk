/**
 * @file AuthService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 认证服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "AuthService.hpp"

#include <functional>
#include <type_traits>

#include <drogon/utils/coroutine.h>
#include <json/writer.h>

#include "dtos/AuthDto.hpp"
#include "models/OperationLogs.hpp"
#include "services/TokenService.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/HashUtil.hpp"
#include "utils/RedisKeyPrefix.hpp"

namespace disk::auth {

    namespace {

        template <typename Func>
        auto RunOnAuthCpuPool(Func func)
            -> drogon::Task<std::remove_cvref_t<std::invoke_result_t<Func&>>> {
            using ReturnType = std::remove_cvref_t<std::invoke_result_t<Func&>>;

            auto* resume_loop = trantor::EventLoop::getEventLoopOfCurrentThread();
            auto result = co_await drogon::queueInLoopCoro<ReturnType>(
                disk::services::detail::GetAuthCpuWorkLoop(),
                std::function<ReturnType()>([func = std::move(func)]() mutable -> ReturnType {
                    return func();
                })
            );

            if (resume_loop != nullptr &&
                resume_loop != trantor::EventLoop::getEventLoopOfCurrentThread()) {
                co_await drogon::switchThreadCoro(resume_loop);
            }

            co_return result;
        }

    } // namespace

    using disk::services::TokenService;
    using disk::utils::ConfigMgr;
    using disk::utils::HashUtil;
    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::Users;

    AuthService::AuthService(const drogon::nosql::RedisClientPtr& redis_client)
        : m_db_client(drogon::app().getDbClient()),
          m_redis_service(disk::services::RedisService::GetInstance()) {
        // 如果尚未初始化，则初始化 RedisService 单例
        disk::services::RedisService::Initialize(redis_client);
        // 初始化 TokenService 单例
        disk::services::TokenService::Initialize(ConfigMgr::GetInstance()->GetJwtSecret());

        LOG_DEBUG << "AuthService initialization completed";
    }

    auto AuthService::Register(RegisterRequest request) -> drogon::Task<Result<RegisterResponse>> {
        LOG_DEBUG << "Starting user registration: " << request.username;

        // 1. 检查用户名是否已存在
        if (co_await IsUsernameExists(request.username)) {
            LOG_WARN << "Username already exists: " << request.username;
            co_return std::unexpected(ErrorInfo(ErrorCode::UsernameExists));
        }

        // 2. 检查邮箱是否已存在
        if (co_await IsEmailExists(request.email)) {
            LOG_WARN << "Email already exists: " << request.email.substr(0, 3) << "***@***";
            co_return std::unexpected(ErrorInfo(ErrorCode::EmailExists));
        }

        // 3. 加密密码（使用 libsodium Argon2id）
        LOG_DEBUG << "Starting password hash: " << request.username;
        auto hash_result = co_await RunOnAuthCpuPool(
            [password = std::move(request.password)]() {
                return HashUtil::HashPassword(password);
            }
        );
        if (!hash_result) {
            LOG_ERROR << "Password hash failed: " << request.username;
            co_return std::unexpected(hash_result.error());
        }
        LOG_DEBUG << "Password hash completed: " << request.username;

        // 4. 创建用户记录
        Users user;
        user.setUsername(request.username);
        user.setEmail(request.email);
        user.setPasswordHash(hash_result.value());
        user.setNickname(request.username); // 默认昵称为用户名
        user.setStorageQuota(DEFAULT_STORAGE_QUOTA);
        user.setStorageUsed(0);
        user.setStatus(1); // 正常状态
        user.setLoginAttempts(0);

        // 5. 插入数据库
        try {
            CoroMapper<Users> mapper(m_db_client);
            user = co_await mapper.insert(user);
            LOG_INFO << "User data inserted successfully: " << request.username
                     << " (ID: " << user.getValueOfId() << ")";
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "User registration database insert failed: " << request.username << " - "
                      << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Registration failed, please try again later")
            );
        }

        // 6. 返回用户信息
        auto response = UserToResponse(user);
        LOG_INFO << "User registration process completed: " << response.username
                 << " (ID: " << response.id << ")";
        co_return response;
    }

    auto AuthService::Login(LoginRequest request, std::string ip_address)
        -> drogon::Task<Result<LoginResponse>> {

        LOG_DEBUG << "User login attempt: " << request.account;

        // 0. 检查 IP 登录频率限制
        const std::string rate_key =
            disk::redis::RedisKeyPrefix::BuildLoginRateLimitKey(ip_address);

        // 使用 Lua 脚本原子递增计数并设置过期时间（单次 Redis 交互）
        auto incr_result = co_await m_redis_service->IncrWithExpire(rate_key, 300);
        if (incr_result.has_value()) {
            const auto count = incr_result.value();

            // 检查是否超过阈值（5 次）
            if (count > 5) {
                const std::string ip_only = disk::redis::RedisKeyPrefix::ExtractIPOnly(ip_address);
                LOG_WARN << "Login rate limit triggered: ip=" << ip_only << ", attempts=" << count;
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::TooManyRequests,
                    "Too many login attempts, please try again in 5 minutes"
                ));
            }
        } else {
            // 失败开放策略：Redis 失败时只记录警告，不阻止登录
            LOG_WARN << "Redis rate limit check failed: " << incr_result.error().message;
        }

        // 1. 查找用户（用户名或邮箱）
        auto user_result = co_await FindUser(request.account);
        if (!user_result) {
            LOG_WARN << "User not found: " << request.account;
            co_return std::unexpected(user_result.error());
        }

        const auto& user = *user_result;

        // 2. 检查账户状态
        const auto status = user.getValueOfStatus();
        if (status == 0) {
            LOG_WARN << "Account disabled: " << request.account;
            co_return std::unexpected(ErrorInfo(ErrorCode::AccountDisabled));
        }

        if (CheckAccountLocked(user)) {
            LOG_WARN << "Account locked: " << request.account;
            co_return std::unexpected(ErrorInfo(ErrorCode::AccountLocked));
        }

        // 3. 验证密码
        const auto stored_password_hash = user.getValueOfPasswordHash();
        auto password_matches = co_await RunOnAuthCpuPool(
            [password = request.password, stored_password_hash]() {
                return HashUtil::VerifyPassword(password, stored_password_hash);
            }
        );
        if (!password_matches) {
            LOG_WARN << "Invalid password: " << request.account;
            co_await IncrementLoginAttempts(user.getValueOfId());
            co_return std::unexpected(ErrorInfo(ErrorCode::InvalidCredentials));
        }

        // 4. 生成令牌
        auto [access_token, refresh_token] =
            TokenService::GetInstance()->GenerateTokens(user.getValueOfId(), user.getValueOfUsername(), user.getValueOfRole(), user.getValueOfStatus());

        // 5. 存储 refresh_token 到 Redis
        auto store_result =
            co_await TokenService::GetInstance()->StoreRefreshToken(user.getValueOfId(), refresh_token);
        if (!store_result.has_value()) {
            LOG_WARN << "Failed to store refresh_token in Redis: " << user.getValueOfId();
        }

        // 6. 更新登录信息
        co_await UpdateLoginInfo(user.getValueOfId(), ip_address);

        // 7. 构造响应
        LoginResponse response;
        response.access_token = access_token;
        response.refresh_token = refresh_token;
        response.token_type = "Bearer";
        response.expires_in = disk::services::TokenService::GetAccessTokenExpireSeconds();
        response.user = UserToResponse(user);

        LOG_INFO << "User login successful: " << request.account << " (ID: " << user.getValueOfId()
                 << ")";
        co_return response;
    }

    auto AuthService::RefreshTokens(RefreshTokenRequest request)
        -> drogon::Task<Result<RefreshTokenResponse>> {
        LOG_DEBUG << "Starting token refresh";

        // 1. 验证刷新令牌
        auto verify_result = TokenService::GetInstance()->VerifyRefreshToken(request.refresh_token);
        if (!verify_result) {
            LOG_WARN << "Refresh token verification failed";
            co_return std::unexpected(verify_result.error());
        }

        const auto [user_id, jti] = verify_result.value();
        LOG_DEBUG << "Refresh token verified successfully: user_id=" << user_id << ", jti=" << jti;

        // 2. 查询用户信息
        try {
            CoroMapper<Users> mapper(m_db_client);

            auto user =
                co_await mapper.findOne(Criteria(Users::Cols::_id, CompareOperator::EQ, user_id));
            LOG_DEBUG << "Found user: " << user.getValueOfUsername();

            // 3. 检查账户状态
            const auto status = user.getValueOfStatus();
            if (status == 0) {
                LOG_WARN << "Account disabled: " << user.getValueOfUsername();
                co_return std::unexpected(ErrorInfo(ErrorCode::AccountDisabled));
            }

            if (CheckAccountLocked(user)) {
                LOG_WARN << "Account locked: " << user.getValueOfUsername();
                co_return std::unexpected(ErrorInfo(ErrorCode::AccountLocked));
            }

            // 4. 生成新的令牌对
            auto [access_token, new_refresh_token] =
                TokenService::GetInstance()->GenerateTokens(user.getValueOfId(), user.getValueOfUsername(), user.getValueOfRole(), user.getValueOfStatus());

            // 5. 刷新 Redis 中的 token（原子操作）
            auto refresh_result = co_await TokenService::GetInstance()->RefreshRefreshToken(
                user.getValueOfId(),
                request.refresh_token,
                new_refresh_token
            );
            if (!refresh_result) {
                LOG_WARN << "Refresh token renewal failed: " << user.getValueOfId();
                co_return std::unexpected(refresh_result.error());
            }

            // 7. 构造响应
            RefreshTokenResponse response;
            response.access_token = access_token;
            response.refresh_token = new_refresh_token;
            response.expires_in = disk::services::TokenService::GetAccessTokenExpireSeconds();

            LOG_INFO << "Token refresh successful: user_id=" << user_id;
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "User query failed: " << user_id << " - " << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::UserNotFound));
        } catch (const std::exception& e) {
            LOG_ERROR << "Token refresh processing failed: " << e.what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Token refresh failed, please try again later")
            );
        }
    }

    auto
    AuthService::Logout(uint64_t user_id, const std::string& access_token, std::string ip_address)
        -> drogon::Task<Result<void>> {

        LOG_INFO << "User logout: user_id=" << user_id << ", ip=" << ip_address;

        // 步骤 1: 使访问令牌失效
        auto invalidate_result = co_await TokenService::GetInstance()->InvalidateAccessToken(access_token);
        if (!invalidate_result.has_value()) {
            LOG_WARN << "Access token invalidation failed: user_id=" << user_id;
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Logout failed, please try again later")
            );
        }

        // 步骤 2: 撤销刷新令牌
        auto revoke_result = co_await TokenService::GetInstance()->RevokeRefreshToken(user_id);
        if (!revoke_result) {
            LOG_WARN << "Refresh token revocation failed: user_id=" << user_id;
            // 不中断流程，继续返回成功
        }

        // 步骤 3: 记录登出日志到 operation_logs
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
            log.setIpAddress(std::string(ip_address));
            log.setCreatedAt(trantor::Date::now());

            co_await mapper.insert(log);
            LOG_DEBUG << "Logout log recorded: user_id=" << user_id;
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_WARN << "Failed to record logout log: " << e.base().what();
            // 不中断流程
        }

        LOG_INFO << "User logout successful: user_id=" << user_id;
        co_return {};
    }

    auto AuthService::IsUsernameExists(std::string username) const -> drogon::Task<bool> {
        try {
            CoroMapper<Users> mapper(m_db_client);
            auto count = co_await mapper.count(Criteria(Users::Cols::_username, username));
            LOG_DEBUG << "Check username existence: " << username << " - "
                      << (count > 0 ? "exists" : "not found");
            co_return count > 0;
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Check username failed: " << username << " - " << e.base().what();
            co_return false;
        }
    }

    auto AuthService::IsEmailExists(std::string email) const -> drogon::Task<bool> {
        try {
            CoroMapper<Users> mapper(m_db_client);
            auto count = co_await mapper.count(Criteria(Users::Cols::_email, email));
            LOG_DEBUG << "Check email existence: " << email.substr(0, 3) << "***@***" << " - "
                      << (count > 0 ? "exists" : "not found");
            co_return count > 0;
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Check email failed: " << email << " - " << e.base().what();
            co_return false;
        }
    }

    auto AuthService::UserToResponse(const Users& user) -> RegisterResponse {
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

    auto AuthService::FindUser(std::string account) const -> drogon::Task<Result<Users>> {

        try {
            CoroMapper<Users> mapper(m_db_client);

            auto by_username = co_await mapper.findOne(
                Criteria(Users::Cols::_username, CompareOperator::EQ, account)
            );
            LOG_DEBUG << "Found user by username: " << account;
            co_return by_username;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_DEBUG << "Username lookup failed: " << account;
        }

        try {
            CoroMapper<Users> mapper(m_db_client);

            auto by_email = co_await mapper.findOne(
                Criteria(Users::Cols::_email, CompareOperator::EQ, account)
            );
            LOG_DEBUG << "Found user by email: " << account;
            co_return by_email;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_DEBUG << "Email lookup failed: " << account;
        }

        LOG_WARN << "User not found: " << account;
        co_return std::unexpected(ErrorInfo(ErrorCode::UserNotFound));
    }

    auto AuthService::CheckAccountLocked(const Users& user) const -> bool {
        // 检查 status 字段（2 = 锁定）
        if (user.getValueOfStatus() == 2) {
            return true;
        }

        // 检查 locked_until 字段
        if (user.getLockedUntil()) {
            const auto& locked_until = user.getValueOfLockedUntil();
            const auto now = trantor::Date::now();
            if (locked_until > now) {
                return true;
            }
        }

        return false;
    }

    auto AuthService::UpdateLoginInfo(uint64_t user_id, std::string ip_address)
        -> drogon::Task<void> {

        try {
            CoroMapper<Users> mapper(m_db_client);

            Users user;
            user.setId(user_id);
            user.setLastLoginAt(trantor::Date::now());
            user.setLastLoginIp(ip_address);
            user.setLoginAttempts(0);

            co_await mapper.update(user);
            LOG_DEBUG << "Login info updated successfully: " << user_id;

            // 清除 IP 频率限制计数器
            const std::string rate_key =
                disk::redis::RedisKeyPrefix::BuildLoginRateLimitKey(ip_address);

            auto delete_result = co_await m_redis_service->Delete(rate_key);
            if (delete_result.has_value()) {
                const std::string ip_only = disk::redis::RedisKeyPrefix::ExtractIPOnly(ip_address);
                LOG_DEBUG << "Login rate limit counter cleared: ip=" << ip_only;
            } else {
                LOG_WARN << "Failed to clear login rate limit counter: "
                         << delete_result.error().message;
            }
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to update login info: " << user_id << " - " << e.base().what();
        }
    }

    auto AuthService::IncrementLoginAttempts(uint64_t user_id) -> drogon::Task<void> {

        try {
            CoroMapper<Users> mapper(m_db_client);

            // 查询当前失败次数
            auto user =
                co_await mapper.findOne(Criteria(Users::Cols::_id, CompareOperator::EQ, user_id));

            auto attempts = user.getValueOfLoginAttempts() + 1;

            // 检查是否需要锁定
            if (attempts >= 5) {
                // 锁定账户 15 分钟
                auto locked_until = trantor::Date::now().after(15 * 60);
                user.setLockedUntil(locked_until);
                user.setStatus(2);
                LOG_WARN << "Account locked: " << user_id << " (unlocks in 15 minutes)";
            } else {
                user.setLoginAttempts(attempts);
                LOG_WARN << "Failed login attempts: " << user_id << " = " << attempts;
            }

            co_await mapper.update(user);

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to increment login attempts: " << user_id << " - "
                      << e.base().what();
        }
    }
} // namespace disk::auth
