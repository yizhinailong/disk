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
#include <filesystem>
#include <format>

#include <drogon/HttpAppFramework.h>
#include <drogon/nosql/RedisClient.h>
#include <drogon/orm/CoroMapper.h>
#include <drogon/orm/Criteria.h>

#include "models/Users.hpp"
#include "utils/ConfigMgr.hpp"

namespace disk::services {

    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::Users;

    AdminService::AdminService()
        : m_db_client(drogon::app().getDbClient()),
          m_start_time(std::chrono::steady_clock::now()) {
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

    auto AdminService::ListUserFiles(uint64_t user_id, const admin::ListUserFilesRequest& req)
        -> drogon::Task<Result<admin::FileListResponse>> {

        LOG_INFO << "Admin list user files: user_id=" << user_id
                 << " page=" << req.page << " page_size=" << req.page_size;

        try {
            CoroMapper<Users> user_mapper(m_db_client);
            co_await user_mapper.findOne(
                Criteria(Users::Cols::_id, CompareOperator::EQ, user_id)
            );

            std::string where_clause = " WHERE user_id = ?";
            std::string count_sql = "SELECT COUNT(*) AS total FROM files" + where_clause;
            std::string query_sql =
                "SELECT id, user_id, folder_id AS parent_id, name, size, "
                "extension, mime_type, path, created_at, updated_at "
                "FROM files" + where_clause;

            int total = 0;
            if (req.folder_id.has_value()) {
                auto count_result = co_await m_db_client->execSqlCoro(
                    "SELECT COUNT(*) AS total FROM files WHERE user_id = ? AND folder_id = ?",
                    user_id, *req.folder_id
                );
                if (!count_result.empty()) {
                    total = count_result[0]["total"].as<int>();
                }
            } else {
                auto count_result = co_await m_db_client->execSqlCoro(
                    "SELECT COUNT(*) AS total FROM files WHERE user_id = ?",
                    user_id
                );
                if (!count_result.empty()) {
                    total = count_result[0]["total"].as<int>();
                }
            }

            int offset = (req.page - 1) * req.page_size;
            int total_pages = req.page_size > 0
                ? static_cast<int>(std::ceil(static_cast<double>(total) / req.page_size))
                : 0;

            auto buildQuerySql = [&](bool has_folder) -> std::string {
                return "SELECT id, user_id, folder_id AS parent_id, name, size, "
                       "extension, mime_type, path, created_at, updated_at "
                       "FROM files WHERE user_id = ?"
                       + std::string(has_folder ? " AND folder_id = ?" : "")
                       + " ORDER BY created_at DESC LIMIT ? OFFSET ?";
            };

            auto result = req.folder_id.has_value()
                ? co_await m_db_client->execSqlCoro(
                    buildQuerySql(true),
                    user_id, *req.folder_id, req.page_size, offset
                )
                : co_await m_db_client->execSqlCoro(
                    buildQuerySql(false),
                    user_id, req.page_size, offset
                );

            admin::FileListResponse response;
            response.pagination.page = req.page;
            response.pagination.page_size = req.page_size;
            response.pagination.total = total;
            response.pagination.total_pages = total_pages;

            for (const auto& row : result) {
                admin::FileDetailResponse file;
                file.id = row["id"].as<uint64_t>();
                file.name = row["name"].as<std::string>();
                file.type = "file";
                file.size = row["size"].as<uint64_t>();
                file.hash = "";
                file.mime_type = row["mime_type"].isNull() ? "" : row["mime_type"].as<std::string>();
                file.parent_id = row["parent_id"].as<uint64_t>();
                file.path = row["path"].isNull() ? "" : row["path"].as<std::string>();
                file.created_at = row["created_at"].as<std::string>();
                file.updated_at = row["updated_at"].as<std::string>();
                response.items.push_back(std::move(file));
            }

            LOG_INFO << "Admin list user files successful: user_id=" << user_id
                     << " total=" << total;
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            const auto error_msg = std::string(e.base().what());
            if (error_msg.find("condition") != std::string::npos ||
                error_msg.find("empty") != std::string::npos) {
                LOG_WARN << "Admin user not found for file listing: user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::AdminUserNotFound));
            }

            LOG_ERROR << "Admin list user files database error: " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to list user files"
            ));
        }
    }

    auto AdminService::GetUserStorage(uint64_t user_id)
        -> drogon::Task<Result<admin::StorageStatsResponse>> {

        LOG_INFO << "Admin get user storage: user_id=" << user_id;

        try {
            CoroMapper<Users> mapper(m_db_client);
            auto user = co_await mapper.findOne(
                Criteria(Users::Cols::_id, CompareOperator::EQ, user_id)
            );

            auto file_count_result = co_await m_db_client->execSqlCoro(
                "SELECT COUNT(*) AS total_files FROM files WHERE user_id = ?",
                user_id
            );

            admin::StorageStatsResponse response;
            response.total_users = 1;
            response.total_files = file_count_result.empty()
                ? 0 : file_count_result[0]["total_files"].as<int>();
            response.total_storage_used = user.getValueOfStorageUsed();
            response.total_storage_quota = user.getValueOfStorageQuota();
            response.active_shares = 0;

            co_await LogOperation(0, "admin.storage.user_stats", user_id,
                std::format(R"({{"user_id": {}, "storage_used": {}, "storage_quota": {}}})",
                    user_id, response.total_storage_used, response.total_storage_quota));

            LOG_INFO << "Admin get user storage successful: user_id=" << user_id;
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            const auto error_msg = std::string(e.base().what());
            if (error_msg.find("condition") != std::string::npos ||
                error_msg.find("empty") != std::string::npos) {
                LOG_WARN << "Admin user not found for storage stats: user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::AdminUserNotFound));
            }

            LOG_ERROR << "Admin get user storage database error: " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to get user storage stats"
            ));
        }
    }

    auto AdminService::GetGlobalStorageStats()
        -> drogon::Task<Result<admin::StorageStatsResponse>> {

        LOG_INFO << "Admin get global storage stats";

        try {
            auto user_stats = co_await m_db_client->execSqlCoro(
                "SELECT COUNT(*) AS total_users, "
                "COALESCE(SUM(storage_used), 0) AS total_storage_used, "
                "COALESCE(SUM(storage_quota), 0) AS total_storage_quota "
                "FROM users"
            );

            auto file_stats = co_await m_db_client->execSqlCoro(
                "SELECT COUNT(*) AS total_files FROM files"
            );

            auto share_stats = co_await m_db_client->execSqlCoro(
                "SELECT COUNT(*) AS active_shares FROM shares WHERE status = 1"
            );

            admin::StorageStatsResponse response;
            response.total_users = user_stats.empty()
                ? 0 : user_stats[0]["total_users"].as<int>();
            response.total_storage_used = user_stats.empty()
                ? 0 : user_stats[0]["total_storage_used"].as<uint64_t>();
            response.total_storage_quota = user_stats.empty()
                ? 0 : user_stats[0]["total_storage_quota"].as<uint64_t>();
            response.total_files = file_stats.empty()
                ? 0 : file_stats[0]["total_files"].as<int>();
            response.active_shares = share_stats.empty()
                ? 0 : share_stats[0]["active_shares"].as<int>();

            co_await LogOperation(0, "admin.storage.global_stats", 0,
                std::format(R"({{"total_users": {}, "total_files": {}, "total_storage_used": {}, "active_shares": {}}})",
                    response.total_users, response.total_files,
                    response.total_storage_used, response.active_shares));

            LOG_INFO << "Admin get global storage stats successful";
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Admin get global storage stats database error: " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to get global storage stats"
            ));
        }
    }

    auto AdminService::ListShares(const admin::ListSharesRequest& req)
        -> drogon::Task<Result<admin::ShareListResponse>> {

        LOG_INFO << "Admin list shares: page=" << req.page
                 << " page_size=" << req.page_size;

        try {
            std::string where_clause = " WHERE 1=1";
            if (req.status.has_value()) {
                where_clause += " AND s.status = " + std::to_string(*req.status);
            }
            if (req.user_id.has_value()) {
                where_clause += " AND s.user_id = " + std::to_string(*req.user_id);
            }

            auto count_result = co_await m_db_client->execSqlCoro(
                "SELECT COUNT(*) AS total FROM shares s" + where_clause
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
                "SELECT s.id, s.user_id, u.username, s.file_id, f.name AS file_name, "
                "s.share_code, s.status, "
                "(s.view_count + s.download_count) AS access_count, "
                "s.created_at, s.expires_at "
                "FROM shares s "
                "LEFT JOIN users u ON s.user_id = u.id "
                "LEFT JOIN files f ON s.file_id = f.id "
                + where_clause +
                " ORDER BY s.created_at DESC LIMIT ? OFFSET ?",
                req.page_size,
                offset
            );

            admin::ShareListResponse response;
            response.pagination.page = req.page;
            response.pagination.page_size = req.page_size;
            response.pagination.total = total;
            response.pagination.total_pages = total_pages;

            for (const auto& row : result) {
                admin::ShareDetailResponse share;
                share.id = row["id"].as<uint64_t>();
                share.user_id = row["user_id"].as<uint64_t>();
                share.username = row["username"].isNull() ? "" : row["username"].as<std::string>();
                share.file_id = row["file_id"].isNull() ? 0 : row["file_id"].as<uint64_t>();
                share.file_name = row["file_name"].isNull() ? "" : row["file_name"].as<std::string>();
                share.share_code = row["share_code"].as<std::string>();
                share.status = row["status"].as<int>();
                share.access_count = row["access_count"].isNull() ? 0 : row["access_count"].as<int>();
                share.created_at = row["created_at"].as<std::string>();
                share.expires_at = row["expires_at"].isNull() ? "" : row["expires_at"].as<std::string>();
                response.items.push_back(std::move(share));
            }

            co_await LogOperation(0, "admin.share.list", 0,
                std::format(R"({{"page": {}, "page_size": {}, "total": {}}})",
                    req.page, req.page_size, total));

            LOG_INFO << "Admin list shares successful: total=" << total;
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Admin list shares database error: " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to list shares"
            ));
        }
    }

    auto AdminService::GetShareDetail(uint64_t share_id)
        -> drogon::Task<Result<admin::ShareDetailResponse>> {

        LOG_INFO << "Admin get share detail: share_id=" << share_id;

        try {
            auto result = co_await m_db_client->execSqlCoro(
                "SELECT s.id, s.user_id, u.username, s.file_id, f.name AS file_name, "
                "s.share_code, s.status, "
                "(s.view_count + s.download_count) AS access_count, "
                "s.created_at, s.expires_at "
                "FROM shares s "
                "LEFT JOIN users u ON s.user_id = u.id "
                "LEFT JOIN files f ON s.file_id = f.id "
                "WHERE s.id = ?",
                share_id
            );

            if (result.empty()) {
                LOG_WARN << "Admin share not found: share_id=" << share_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::AdminShareNotFound));
            }

            const auto& row = result[0];
            admin::ShareDetailResponse response;
            response.id = row["id"].as<uint64_t>();
            response.user_id = row["user_id"].as<uint64_t>();
            response.username = row["username"].isNull() ? "" : row["username"].as<std::string>();
            response.file_id = row["file_id"].isNull() ? 0 : row["file_id"].as<uint64_t>();
            response.file_name = row["file_name"].isNull() ? "" : row["file_name"].as<std::string>();
            response.share_code = row["share_code"].as<std::string>();
            response.status = row["status"].as<int>();
            response.access_count = row["access_count"].isNull() ? 0 : row["access_count"].as<int>();
            response.created_at = row["created_at"].as<std::string>();
            response.expires_at = row["expires_at"].isNull() ? "" : row["expires_at"].as<std::string>();

            co_await LogOperation(0, "admin.share.detail", share_id,
                std::format(R"({{"share_id": {}}})", share_id));

            LOG_INFO << "Admin get share detail successful: share_id=" << share_id;
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Admin get share detail database error: share_id=" << share_id
                      << " - " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to get share detail"
            ));
        }
    }

    auto AdminService::ForceCancelShare(uint64_t share_id, uint64_t operator_id)
        -> drogon::Task<Result<void>> {

        LOG_INFO << "Admin force cancel share: share_id=" << share_id
                 << " operator_id=" << operator_id;

        try {
            auto result = co_await m_db_client->execSqlCoro(
                "SELECT id, share_code, user_id, status FROM shares WHERE id = ?",
                share_id
            );

            if (result.empty()) {
                LOG_WARN << "Admin share not found for force cancel: share_id=" << share_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::AdminShareNotFound));
            }

            auto current_status = result[0]["status"].as<int>();
            if (current_status == 0) {
                LOG_WARN << "Admin share already cancelled: share_id=" << share_id;
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "Share already cancelled"
                ));
            }

            co_await m_db_client->execSqlCoro(
                "UPDATE shares SET status = 0, updated_at = NOW() WHERE id = ?",
                share_id
            );

            auto share_code = result[0]["share_code"].as<std::string>();
            auto owner_id = result[0]["user_id"].as<uint64_t>();
            co_await LogOperation(operator_id, "admin.share.force_cancel", share_id,
                std::format(R"({{"share_id": {}, "share_code": "{}", "owner_id": {}, "previous_status": {}}})",
                    share_id, share_code, owner_id, current_status));

            LOG_INFO << "Admin force cancel share successful: share_id=" << share_id;
            co_return {};

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Admin force cancel share database error: " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to force cancel share"
            ));
        }
    }

    auto AdminService::GetOverviewStats()
        -> drogon::Task<Result<admin::StorageStatsResponse>> {

        LOG_INFO << "admin.stats.overview";

        try {
            auto user_stats = co_await m_db_client->execSqlCoro(
                "SELECT COUNT(*) AS total_users, "
                "COALESCE(SUM(storage_used), 0) AS total_storage_used, "
                "COALESCE(SUM(storage_quota), 0) AS total_storage_quota "
                "FROM users"
            );

            auto file_stats = co_await m_db_client->execSqlCoro(
                "SELECT COUNT(*) AS total_files FROM files"
            );

            auto share_stats = co_await m_db_client->execSqlCoro(
                "SELECT COUNT(*) AS active_shares FROM shares WHERE status = 1"
            );

            admin::StorageStatsResponse response;
            response.total_users = user_stats.empty()
                ? 0 : user_stats[0]["total_users"].as<int>();
            response.total_storage_used = user_stats.empty()
                ? 0 : user_stats[0]["total_storage_used"].as<uint64_t>();
            response.total_storage_quota = user_stats.empty()
                ? 0 : user_stats[0]["total_storage_quota"].as<uint64_t>();
            response.total_files = file_stats.empty()
                ? 0 : file_stats[0]["total_files"].as<int>();
            response.active_shares = share_stats.empty()
                ? 0 : share_stats[0]["active_shares"].as<int>();

            LOG_INFO << "admin.stats.overview successful";
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "admin.stats.overview database error: " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to get overview stats"
            ));
        }
    }

    auto AdminService::GetSystemStatus()
        -> drogon::Task<Result<admin::SystemStatusResponse>> {

        LOG_INFO << "admin.stats.system";

        admin::SystemStatusResponse response;

        // MySQL check
        try {
            co_await m_db_client->execSqlCoro("SELECT 1");
            response.mysql_connected = true;
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_WARN << "admin.stats.system MySQL check failed: " << e.base().what();
            response.mysql_connected = false;
        }

        // Redis check
        try {
            auto redis_client = drogon::app().getRedisClient();
            if (redis_client) {
                auto result = co_await redis_client->execCommandCoro("ping");
                response.redis_connected = !result.isNil() && result.asString() == "PONG";
            } else {
                response.redis_connected = false;
            }
        } catch (const drogon::nosql::RedisException& e) {
            LOG_WARN << "admin.stats.system Redis check failed: " << e.what();
            response.redis_connected = false;
        } catch (const std::exception& e) {
            LOG_WARN << "admin.stats.system Redis check failed: " << e.what();
            response.redis_connected = false;
        }

        // Disk space
        try {
            auto storage_path = utils::ConfigMgr::GetInstance()->GetStorageBasePath();
            auto space_info = std::filesystem::space(storage_path);
            response.disk_total = space_info.capacity;
            response.disk_free = space_info.available;
            response.disk_used = space_info.capacity - space_info.available;
        } catch (const std::filesystem::filesystem_error& e) {
            LOG_WARN << "admin.stats.system disk space check failed: " << e.what();
            response.disk_total = 0;
            response.disk_used = 0;
            response.disk_free = 0;
        }

        // Uptime
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - m_start_time);
        response.uptime_seconds = static_cast<uint64_t>(uptime.count());

        LOG_INFO << "admin.stats.system successful";
        co_return response;
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
