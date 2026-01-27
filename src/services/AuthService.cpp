/**
 * @file AuthService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 认证服务实现
 * @version 0.1
 * @date 2026-01-14
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "AuthService.hpp"

#include "dtos/AuthDto.hpp"
#include "models/OperationLogs.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/HashUtil.hpp"

namespace disk::auth {

    using disk::utils::ConfigMgr;
    using disk::utils::HashUtil;
    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::Users;

    AuthService::AuthService(const drogon::nosql::RedisClientPtr& redis_client)
        : m_db_client(drogon::app().getDbClient()),
          m_redis_service(std::make_shared<disk::services::RedisService>(redis_client)),
          m_token_service(
              std::make_unique<TokenService>(ConfigMgr::GetInstance()->GetJwtSecret(), m_redis_service)
          ) {
        LOG_DEBUG << "AuthService 初始化完成";
    }

    auto AuthService::Register(RegisterRequest request) -> drogon::Task<Result<RegisterResponse>> {
        LOG_DEBUG << "开始注册用户: " << request.username << " <" << request.email << ">";

        // 1. 检查用户名是否已存在
        if (co_await IsUsernameExists(request.username)) {
            LOG_WARN << "用户名已存在: " << request.username;
            co_return std::unexpected(ErrorInfo(ErrorCode::UsernameExists));
        }

        // 2. 检查邮箱是否已存在
        if (co_await IsEmailExists(request.email)) {
            LOG_WARN << "邮箱已存在: " << request.email;
            co_return std::unexpected(ErrorInfo(ErrorCode::EmailExists));
        }

        // 3. 加密密码（使用 libsodium Argon2id）
        LOG_DEBUG << "开始密码哈希计算: " << request.username;
        auto hash_result = HashUtil::HashPassword(request.password);
        if (!hash_result) {
            LOG_ERROR << "密码哈希失败: " << request.username;
            co_return std::unexpected(hash_result.error());
        }
        LOG_DEBUG << "密码哈希完成: " << request.username;

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
            LOG_INFO << "用户数据插入成功: " << request.username << " (ID: " << user.getValueOfId() << ")";
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "用户注册数据库插入失败: " << request.username << " - " << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "注册失败，请稍后重试"));
        }

        // 6. 返回用户信息
        auto response = UserToResponse(user);
        LOG_INFO << "用户注册流程完成: " << response.username << " (ID: " << response.id << ")";
        co_return response;
    }

    auto AuthService::Login(LoginRequest request, std::string ip_address)
        -> drogon::Task<Result<LoginResponse>> {

        LOG_DEBUG << "用户登录尝试: " << request.account;

        // 0. 检查 IP 登录频率限制
        std::string ip_only = ip_address;
        const auto colon_pos = ip_only.find(':');
        if (colon_pos != std::string::npos) {
            ip_only = ip_only.substr(0, colon_pos);
        }
        const std::string rate_key = "rate:login:" + ip_only;

        auto incr_result = co_await m_redis_service->Incr(rate_key);
        if (incr_result.has_value()) {
            const auto count = incr_result.value();

            // 首次计数时设置 TTL 为 300 秒（5 分钟）
            if (count == 1) {
                auto expire_result = co_await m_redis_service->Expire(rate_key, 300);
                if (!expire_result.has_value()) {
                    LOG_WARN << "设置频率限制 TTL 失败: " << expire_result.error().message;
                }
            }

            // 检查是否超过阈值（5 次）
            if (count > 5) {
                LOG_WARN << "登录频率限制触发: ip=" << ip_only << ", attempts=" << count;
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::TooManyRequests,
                    "登录尝试过于频繁，请 5 分钟后重试"
                ));
            }
        } else {
            // Fail-open: Redis 失败时只记录警告，不阻止登录
            LOG_WARN << "Redis 频率限制检查失败: " << incr_result.error().message;
        }

        // 1. 查找用户（用户名或邮箱）
        auto user_result = co_await FindUser(request.account);
        if (!user_result) {
            LOG_WARN << "用户不存在: " << request.account;
            co_return std::unexpected(user_result.error());
        }

        const auto& user = *user_result;

        // 2. 检查账户状态
        const auto status = user.getValueOfStatus();
        if (status == 0) {
            LOG_WARN << "账户已禁用: " << request.account;
            co_return std::unexpected(ErrorInfo(ErrorCode::AccountDisabled));
        }

        if (CheckAccountLocked(user)) {
            LOG_WARN << "账户已锁定: " << request.account;
            co_return std::unexpected(ErrorInfo(ErrorCode::AccountLocked));
        }

        // 3. 验证密码
        if (!HashUtil::VerifyPassword(request.password, user.getValueOfPasswordHash())) {
            LOG_WARN << "密码错误: " << request.account;
            co_await IncrementLoginAttempts(user.getValueOfId());
            co_return std::unexpected(ErrorInfo(ErrorCode::InvalidCredentials));
        }

        // 4. 生成令牌
        auto [access_token, refresh_token] = m_token_service->GenerateTokens(
            user.getValueOfId(),
            user.getValueOfUsername()
        );

        // 5. 存储 refresh_token 到 Redis
        auto store_result = co_await m_token_service->StoreRefreshToken(
            user.getValueOfId(),
            refresh_token
        );
        if (!store_result.has_value()) {
            LOG_WARN << "存储 refresh_token 到 Redis 失败: " << user.getValueOfId();
        }

        // 6. 更新登录信息
        co_await UpdateLoginInfo(user.getValueOfId(), ip_address);

        // 6. 构造响应
        LoginResponse response;
        response.access_token = access_token;
        response.refresh_token = refresh_token;
        response.token_type = "Bearer";
        response.expires_in = TokenService::GetAccessTokenExpireSeconds();
        response.user = UserToResponse(user);

        LOG_INFO << "用户登录成功: " << request.account << " (ID: " << user.getValueOfId() << ")";
        co_return response;
    }

    auto AuthService::RefreshTokens(RefreshTokenRequest request)
        -> drogon::Task<Result<RefreshTokenResponse>> {
        LOG_DEBUG << "开始刷新令牌";

        // 1. 验证刷新令牌
        auto verify_result = m_token_service->VerifyRefreshToken(request.refresh_token);
        if (!verify_result) {
            LOG_WARN << "刷新令牌验证失败";
            co_return std::unexpected(verify_result.error());
        }

        const auto [user_id, jti] = verify_result.value();
        LOG_DEBUG << "刷新令牌验证成功: user_id=" << user_id << ", jti=" << jti;

        // 2. 查询用户信息
        try {
            CoroMapper<Users> mapper(m_db_client);

            auto user = co_await mapper.findOne(
                Criteria(Users::Cols::_id, CompareOperator::EQ, user_id)
            );
            LOG_DEBUG << "找到用户: " << user.getValueOfUsername();

            // 3. 检查账户状态
            const auto status = user.getValueOfStatus();
            if (status == 0) {
                LOG_WARN << "账户已禁用: " << user.getValueOfUsername();
                co_return std::unexpected(ErrorInfo(ErrorCode::AccountDisabled));
            }

            if (CheckAccountLocked(user)) {
                LOG_WARN << "账户已锁定: " << user.getValueOfUsername();
                co_return std::unexpected(ErrorInfo(ErrorCode::AccountLocked));
            }

            // 4. 生成新的令牌对
            auto [access_token, new_refresh_token] = m_token_service->GenerateTokens(
                user.getValueOfId(),
                user.getValueOfUsername()
            );

            // 5. 刷新 Redis 中的 token（原子操作）
            auto refresh_result = co_await m_token_service->RefreshRefreshToken(
                user.getValueOfId(),
                request.refresh_token,
                new_refresh_token
            );
            if (!refresh_result) {
                LOG_WARN << "Refresh token 刷新失败: " << user.getValueOfId();
                co_return std::unexpected(refresh_result.error());
            }

            // 6. 构造响应
            RefreshTokenResponse response;
            response.access_token = access_token;
            response.refresh_token = new_refresh_token;
            response.expires_in = TokenService::GetAccessTokenExpireSeconds();

            LOG_INFO << "令牌刷新成功: user_id=" << user_id;
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "查询用户失败: " << user_id << " - " << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::UserNotFound));
        } catch (const std::exception& e) {
            LOG_ERROR << "刷新令牌处理失败: " << e.what();
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "刷新令牌失败，请稍后重试"));
        }
    }

    auto AuthService::Logout(uint64_t user_id, const std::string& access_token, std::string ip_address)
        -> drogon::Task<Result<void>> {

        LOG_INFO << "用户登出: user_id=" << user_id << ", ip=" << ip_address;

        // 步骤 1: 使访问令牌失效
        auto invalidate_result = co_await m_token_service->InvalidateAccessToken(access_token);
        if (!invalidate_result.has_value()) {
            LOG_WARN << "访问令牌失效失败: user_id=" << user_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "登出失败，请稍后重试"));
        }

        // 步骤 2: 撤销刷新令牌
        auto revoke_result = co_await m_token_service->RevokeRefreshToken(user_id);
        if (!revoke_result) {
            LOG_WARN << "刷新令牌撤销失败: user_id=" << user_id;
            // 不中断流程，继续返回成功
        }

        // 步骤 3: 记录登出日志到 operation_logs
        try {
            drogon::orm::CoroMapper<drogon_model::disk::OperationLogs> mapper(m_db_client);

            drogon_model::disk::OperationLogs log;
            log.setUserId(user_id);
            log.setAction("logout");
            log.setTargetId(0); // 登出操作无 target
            log.setDetails("User logged out");
            log.setIpAddress(std::string(ip_address));
            log.setCreatedAt(trantor::Date::now());

            co_await mapper.insert(log);
            LOG_DEBUG << "登出日志已记录: user_id=" << user_id;
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_WARN << "记录登出日志失败: " << e.base().what();
            // 不中断流程
        }

        LOG_INFO << "用户登出成功: user_id=" << user_id;
        co_return {};
    }

    auto AuthService::IsUsernameExists(std::string username) const -> drogon::Task<bool> {
        try {
            CoroMapper<Users> mapper(m_db_client);
            auto count = co_await mapper.count(Criteria(Users::Cols::_username, username));
            LOG_DEBUG << "检查用户名存在性: " << username << " - " << (count > 0 ? "存在" : "不存在");
            co_return count > 0;
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "检查用户名失败: " << username << " - " << e.base().what();
            co_return false;
        }
    }

    auto AuthService::IsEmailExists(std::string email) const -> drogon::Task<bool> {
        try {
            CoroMapper<Users> mapper(m_db_client);
            auto count = co_await mapper.count(Criteria(Users::Cols::_email, email));
            LOG_DEBUG << "检查邮箱存在性: " << email << " - " << (count > 0 ? "存在" : "不存在");
            co_return count > 0;
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "检查邮箱失败: " << email << " - " << e.base().what();
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

    auto AuthService::FindUser(std::string account) const
        -> drogon::Task<Result<Users>> {

        try {
            CoroMapper<Users> mapper(m_db_client);

            auto by_username = co_await mapper.findOne(
                Criteria(Users::Cols::_username, CompareOperator::EQ, account)
            );
            LOG_DEBUG << "通过用户名找到用户: " << account;
            co_return by_username;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_DEBUG << "使用用户名查找失败: " << account;
        }

        try {
            CoroMapper<Users> mapper(m_db_client);

            auto by_email = co_await mapper.findOne(
                Criteria(Users::Cols::_email, CompareOperator::EQ, account)
            );
            LOG_DEBUG << "通过邮箱找到用户: " << account;
            co_return by_email;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_DEBUG << "使用邮箱查找失败: " << account;
        }

        LOG_WARN << "用户不存在: " << account;
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
            LOG_DEBUG << "更新登录信息成功: " << user_id;

            // 清除 IP 频率限制计数器
            std::string ip_only = ip_address;
            const auto colon_pos = ip_only.find(':');
            if (colon_pos != std::string::npos) {
                ip_only = ip_only.substr(0, colon_pos);
            }
            const std::string rate_key = "rate:login:" + ip_only;

            auto delete_result = co_await m_redis_service->Delete(rate_key);
            if (delete_result.has_value()) {
                LOG_DEBUG << "清除登录频率限制计数器: ip=" << ip_only;
            } else {
                LOG_WARN << "清除登录频率限制计数器失败: " << delete_result.error().message;
            }
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "更新登录信息失败: " << user_id << " - " << e.base().what();
        }
    }

    auto AuthService::IncrementLoginAttempts(uint64_t user_id) -> drogon::Task<void> {

        try {
            CoroMapper<Users> mapper(m_db_client);

            // 查询当前失败次数
            auto user = co_await mapper.findOne(
                Criteria(Users::Cols::_id, CompareOperator::EQ, user_id)
            );

            auto attempts = user.getValueOfLoginAttempts() + 1;

            // 检查是否需要锁定
            if (attempts >= 5) {
                // 锁定账户 15 分钟
                auto locked_until = trantor::Date::now().after(15 * 60);
                user.setLockedUntil(locked_until);
                user.setStatus(2);
                LOG_WARN << "账户已锁定: " << user_id << " (15分钟后解锁)";
            } else {
                user.setLoginAttempts(attempts);
                LOG_WARN << "登录失败次数: " << user_id << " = " << attempts;
            }

            co_await mapper.update(user);

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "增加登录失败次数失败: " << user_id << " - " << e.base().what();
        }
    }
} // namespace disk::auth
