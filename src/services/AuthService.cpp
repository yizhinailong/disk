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

namespace disk::auth {

    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::Users;

    AuthService::AuthService(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {}

    auto AuthService::Register(RegisterRequest request) -> drogon::Task<Result<RegisterResponse>> {

        // 1. 检查用户名是否已存在
        if (co_await IsUsernameExists(request.username)) {
            co_return std::unexpected(ErrorInfo(ErrorCode::UsernameExists));
        }

        // 2. 检查邮箱是否已存在
        if (co_await IsEmailExists(request.email)) {
            co_return std::unexpected(ErrorInfo(ErrorCode::EmailExists));
        }

        // 3. 加密密码（使用 libsodium Argon2id）
        auto password_hash = HashPassword(request.password);

        // 4. 创建用户记录
        Users user;
        user.setUsername(request.username);
        user.setEmail(request.email);
        user.setPasswordHash(password_hash);
        user.setNickname(request.username); // 默认昵称为用户名
        user.setStorageQuota(DEFAULT_STORAGE_QUOTA);
        user.setStorageUsed(0);
        user.setStatus(1); // 正常状态
        user.setLoginAttempts(0);

        // 5. 插入数据库
        try {
            CoroMapper<Users> mapper(m_db_client);
            co_await mapper.insert(user);
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "注册用户失败: " << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "注册失败，请稍后重试"));
        }

        // 6. 返回用户信息
        co_return UserToResponse(user);
    }

    auto AuthService::IsUsernameExists(std::string username) const -> drogon::Task<bool> {
        try {
            CoroMapper<Users> mapper(m_db_client);
            auto count = co_await mapper.count(Criteria(Users::Cols::_username, username));
            co_return count > 0;
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "检查用户名失败: " << e.base().what();
            co_return false;
        }
    }

    auto AuthService::IsEmailExists(std::string email) const -> drogon::Task<bool> {
        try {
            CoroMapper<Users> mapper(m_db_client);
            auto count = co_await mapper.count(Criteria(Users::Cols::_email, email));
            co_return count > 0;
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "检查邮箱失败: " << e.base().what();
            co_return false;
        }
    }

    auto AuthService::HashPassword(const std::string& password) -> std::string {
        // 使用 libsodium 的 Argon2id 算法
        // crypto_pwhash_STRBYTES 是输出缓冲区的大小（128 字节）
        char hashed_password[crypto_pwhash_STRBYTES];

        if (crypto_pwhash_str(
                hashed_password,
                password.c_str(),
                password.length(),
                crypto_pwhash_OPSLIMIT_INTERACTIVE, // 适合交互式应用的计算强度
                crypto_pwhash_MEMLIMIT_INTERACTIVE  // 适合交互式应用的内存限制
                ) != 0) {
            throw std::runtime_error("内存不足，密码哈希失败");
        }

        return std::string(hashed_password);
    }

    auto AuthService::VerifyPassword(const std::string& password, const std::string& hash) -> bool {
        // 使用 libsodium 验证密码
        // crypto_pwhash_str_verify 会自动识别存储的哈希格式（Argon2id）
        return crypto_pwhash_str_verify(hash.c_str(), password.c_str(), password.length()) == 0;
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
