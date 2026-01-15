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

#include "utils/PasswdHash.hpp"

namespace disk::auth {

    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::Users;

    AuthService::AuthService(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {}

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
        auto hash_result = PasswdHash::Hash(request.password);
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
        response.created_at = user.getValueOfCreatedAt().toDbStringLocal();
        return response;
    }
} // namespace disk::auth
