/**
 * @file AdminService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 管理员用户管理服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "AdminService.hpp"

#include <cmath>
#include <format>

#include <drogon/HttpAppFramework.h>
#include <drogon/orm/CoroMapper.h>
#include <drogon/orm/Criteria.h>

#include "models/Users.hpp"

namespace disk::services {

    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::Users;

    AdminService::AdminService()
        : m_db_client(drogon::app().getDbClient()) {
        LOG_DEBUG << "AdminService initialization completed";
    }

    auto AdminService::ListUsers(const admin::ListUsersRequest& req)
        -> drogon::Task<Result<admin::UserListResponse>> {

        LOG_INFO << "Admin list users: page=" << req.page
                 << " page_size=" << req.page_size;

        try {
            std::string where_clause = " WHERE 1=1";
            if (req.username.has_value()) {
                where_clause += " AND username LIKE '%" + *req.username + "%'";
            }
            if (req.email.has_value()) {
                where_clause += " AND email LIKE '%" + *req.email + "%'";
            }
            if (req.status.has_value()) {
                where_clause += " AND status = " + std::to_string(*req.status);
            }
            if (req.role.has_value()) {
                where_clause += " AND role = " + std::to_string(*req.role);
            }

            auto count_result = co_await m_db_client->execSqlCoro(
                "SELECT COUNT(*) AS total FROM users" + where_clause
            );

            int total = 0;
            if (!count_result.empty()) {
                total = count_result[0]["total"].as<int>();
            }

            int offset = (req.page - 1) * req.page_size;
            int total_pages = req.page_size > 0
                ? static_cast<int>(std::ceil(static_cast<double>(total) / req.page_size))
                : 0;

            auto result = co_await m_db_client->execSqlCoro(
                "SELECT id, username, email, nickname, avatar, "
                "role, status, storage_quota, storage_used, storage_reserved, "
                "created_at, last_login_at "
                "FROM users" + where_clause +
                " ORDER BY created_at DESC LIMIT ? OFFSET ?",
                req.page_size,
                offset
            );

            admin::UserListResponse response;
            response.pagination.page = req.page;
            response.pagination.page_size = req.page_size;
            response.pagination.total = total;
            response.pagination.total_pages = total_pages;

            for (const auto& row : result) {
                admin::UserDetailResponse user;
                user.id = row["id"].as<uint64_t>();
                user.username = row["username"].as<std::string>();
                user.email = row["email"].as<std::string>();
                user.nickname = row["nickname"].isNull() ? "" : row["nickname"].as<std::string>();
                user.avatar = row["avatar"].isNull() ? "" : row["avatar"].as<std::string>();
                user.role = row["role"].as<int>();
                user.status = row["status"].as<int>();
                user.storage_quota = row["storage_quota"].as<uint64_t>();
                user.storage_used = row["storage_used"].as<uint64_t>();
                user.storage_reserved = row["storage_reserved"].as<uint64_t>();
                user.created_at = row["created_at"].as<std::string>();
                user.last_login_at = row["last_login_at"].isNull() ? "" : row["last_login_at"].as<std::string>();
                response.items.push_back(std::move(user));
            }

            LOG_INFO << "Admin list users successful: total=" << total;
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Admin list users database error: " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to list users"
            ));
        }
    }

    auto AdminService::GetUserDetail(uint64_t user_id)
        -> drogon::Task<Result<admin::UserDetailResponse>> {

        LOG_INFO << "Admin get user detail: user_id=" << user_id;

        try {
            CoroMapper<Users> mapper(m_db_client);
            auto user = co_await mapper.findOne(
                Criteria(Users::Cols::_id, CompareOperator::EQ, user_id)
            );

            admin::UserDetailResponse response;
            response.id = user.getValueOfId();
            response.username = user.getValueOfUsername();
            response.email = user.getValueOfEmail();
            response.nickname = user.getNickname() ? *user.getNickname() : "";
            response.avatar = user.getAvatar() ? *user.getAvatar() : "";
            response.role = user.getValueOfRole();
            response.status = user.getValueOfStatus();
            response.storage_quota = user.getValueOfStorageQuota();
            response.storage_used = user.getValueOfStorageUsed();
            response.storage_reserved = user.getValueOfStorageReserved();
            response.created_at = user.getValueOfCreatedAt().toDbStringLocal();
            response.last_login_at = user.getLastLoginAt()
                ? user.getValueOfLastLoginAt().toDbStringLocal() : "";

            LOG_INFO << "Admin get user detail successful: user_id=" << user_id;
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            const auto error_msg = std::string(e.base().what());
            if (error_msg.find("condition") != std::string::npos ||
                error_msg.find("empty") != std::string::npos) {
                LOG_WARN << "Admin user not found: user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::AdminUserNotFound));
            }

            LOG_ERROR << "Admin get user detail database error: user_id=" << user_id
                      << " - " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to get user detail"
            ));
        }
    }

    auto AdminService::ChangeUserStatus(uint64_t target_id, int status, uint64_t operator_id)
        -> drogon::Task<Result<void>> {

        LOG_INFO << "Admin change user status: target_id=" << target_id
                 << " status=" << status << " operator_id=" << operator_id;

        if (target_id == operator_id) {
            LOG_WARN << "Admin cannot modify self: operator_id=" << operator_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::AdminCannotModifySelf));
        }

        try {
            CoroMapper<Users> mapper(m_db_client);

            auto user = co_await mapper.findOne(
                Criteria(Users::Cols::_id, CompareOperator::EQ, target_id)
            );

            user.setStatus(static_cast<int8_t>(status));
            co_await mapper.update(user);

            auto details = std::format(
                R"({{"target_id": {}, "status": {}}})",
                target_id, status
            );
            co_await LogOperation(operator_id, "admin.user.status_change", target_id, details);

            LOG_INFO << "Admin change user status successful: target_id=" << target_id;
            co_return {};

        } catch (const drogon::orm::DrogonDbException& e) {
            const auto error_msg = std::string(e.base().what());
            if (error_msg.find("condition") != std::string::npos ||
                error_msg.find("empty") != std::string::npos) {
                LOG_WARN << "Admin user not found for status change: target_id=" << target_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::AdminUserNotFound));
            }

            LOG_ERROR << "Admin change user status database error: " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to change user status"
            ));
        }
    }

    auto AdminService::ChangeUserRole(uint64_t target_id, int role, uint64_t operator_id)
        -> drogon::Task<Result<void>> {

        LOG_INFO << "Admin change user role: target_id=" << target_id
                 << " role=" << role << " operator_id=" << operator_id;

        if (target_id == operator_id) {
            LOG_WARN << "Admin cannot modify self: operator_id=" << operator_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::AdminCannotModifySelf));
        }

        if (role == 0) {
            try {
                auto count_result = co_await m_db_client->execSqlCoro(
                    "SELECT COUNT(*) AS cnt FROM users WHERE role = 1"
                );
                if (!count_result.empty()) {
                    int admin_count = count_result[0]["cnt"].as<int>();
                    if (admin_count <= 1) {
                        LOG_WARN << "Cannot demote last admin: target_id=" << target_id;
                        co_return std::unexpected(ErrorInfo(ErrorCode::AdminCannotDemoteLast));
                    }
                }
            } catch (const drogon::orm::DrogonDbException& e) {
                LOG_ERROR << "Failed to count admins: " << e.base().what();
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::InternalError,
                    "Failed to verify admin count"
                ));
            }
        }

        try {
            CoroMapper<Users> mapper(m_db_client);

            auto user = co_await mapper.findOne(
                Criteria(Users::Cols::_id, CompareOperator::EQ, target_id)
            );

            user.setRole(static_cast<int8_t>(role));
            co_await mapper.update(user);

            auto details = std::format(
                R"({{"target_id": {}, "role": {}}})",
                target_id, role
            );
            co_await LogOperation(operator_id, "admin.user.role_change", target_id, details);

            LOG_INFO << "Admin change user role successful: target_id=" << target_id;
            co_return {};

        } catch (const drogon::orm::DrogonDbException& e) {
            const auto error_msg = std::string(e.base().what());
            if (error_msg.find("condition") != std::string::npos ||
                error_msg.find("empty") != std::string::npos) {
                LOG_WARN << "Admin user not found for role change: target_id=" << target_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::AdminUserNotFound));
            }

            LOG_ERROR << "Admin change user role database error: " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to change user role"
            ));
        }
    }

    auto AdminService::SoftDeleteUser(uint64_t target_id, uint64_t operator_id)
        -> drogon::Task<Result<void>> {

        LOG_INFO << "Admin soft delete user: target_id=" << target_id
                 << " operator_id=" << operator_id;

        if (target_id == operator_id) {
            LOG_WARN << "Admin cannot delete self: operator_id=" << operator_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::AdminCannotModifySelf));
        }

        try {
            CoroMapper<Users> mapper(m_db_client);

            auto user = co_await mapper.findOne(
                Criteria(Users::Cols::_id, CompareOperator::EQ, target_id)
            );

            user.setStatus(static_cast<int8_t>(0));
            co_await mapper.update(user);

            auto details = std::format(
                R"({{"target_id": {}, "username": "{}"}})",
                target_id, user.getValueOfUsername()
            );
            co_await LogOperation(operator_id, "admin.user.soft_delete", target_id, details);

            LOG_INFO << "Admin soft delete user successful: target_id=" << target_id;
            co_return {};

        } catch (const drogon::orm::DrogonDbException& e) {
            const auto error_msg = std::string(e.base().what());
            if (error_msg.find("condition") != std::string::npos ||
                error_msg.find("empty") != std::string::npos) {
                LOG_WARN << "Admin user not found for soft delete: target_id=" << target_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::AdminUserNotFound));
            }

            LOG_ERROR << "Admin soft delete user database error: " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to soft delete user"
            ));
        }
    }

    auto AdminService::LogOperation(uint64_t operator_id,
                                     const std::string& action,
                                     uint64_t target_id,
                                     const std::string& details) -> drogon::Task<void> {
        try {
            co_await m_db_client->execSqlCoro(
                "INSERT INTO operation_logs (user_id, action, target_type, target_id, details) "
                "VALUES (?, ?, 'user', ?, ?)",
                operator_id,
                action,
                target_id,
                details
            );
            LOG_DEBUG << "Operation logged: " << action << " by user_id=" << operator_id;
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to log operation: " << e.base().what();
        }
    }

} // namespace disk::services
