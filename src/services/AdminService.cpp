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
#include <limits>
#include <utility>

#include <drogon/HttpAppFramework.h>
#include <drogon/nosql/RedisClient.h>
#include <drogon/orm/CoroMapper.h>
#include <drogon/orm/Criteria.h>
#include <json/writer.h>

#include "models/Users.hpp"
#include "services/ObservedDbClient.hpp"
#include "services/RedisService.hpp"
#include "utils/ConfigMgr.hpp"

namespace disk::services {

    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::Users;

    namespace {
        [[nodiscard]] auto SerializeDetails(const Json::Value& details) -> std::string {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            return Json::writeString(builder, details);
        }

        auto SetLogContext(
            Json::Value& details,
            const disk::utils::LogContext& log_context
        ) -> void {
            details["request_id"] =
                log_context.request_id.has_value() && !log_context.request_id->empty() ?
                    Json::Value(*log_context.request_id) :
                    Json::Value(Json::nullValue);
            details["operation"] =
                log_context.operation.has_value() && !log_context.operation->empty() ?
                    Json::Value(*log_context.operation) :
                    Json::Value(Json::nullValue);
        }
    } // namespace

    AdminService::AdminService()
        : m_db_client(disk::metrics::ObserveDbClient(drogon::app().getDbClient())),
          m_start_time(std::chrono::steady_clock::now()) {
        Logger::Debug(disk::utils::ServiceRuntimeLogContext()) << "Service initialized: service=admin";
    }

    auto AdminService::ListUsers(
        const admin::ListUsersRequest& req,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<admin::UserListResponse>> {

        Logger::Info(log_context) << "Admin list users: page=" << req.page
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
            int total_pages = req.page_size > 0 ? static_cast<int>(std::ceil(static_cast<double>(total) / req.page_size)) : 0;

            auto result = co_await m_db_client->execSqlCoro(
                "SELECT id, username, email, nickname, avatar, " "role, status, storage_quota, storage_used, storage_reserved, " "created_at, last_login_at " "FROM users" + where_clause +
                    " ORDER BY created_at DESC LIMIT $1 OFFSET $2",
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

            Logger::Info(log_context) << "Admin list users successful: total=" << total;
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error(log_context)
                << "Admin list users database error: " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to list users"
            ));
        }
    }

    auto AdminService::GetUserDetail(
        uint64_t user_id,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<admin::UserDetailResponse>> {

        Logger::Info(log_context) << "Admin get user detail: user_id=" << user_id;

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
            response.last_login_at = user.getLastLoginAt() ? user.getValueOfLastLoginAt().toDbStringLocal() : "";

            Logger::Info(log_context)
                << "Admin get user detail successful: user_id=" << user_id;
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            const auto error_msg = std::string(e.base().what());
            if (error_msg.find("condition") != std::string::npos ||
                error_msg.find("empty") != std::string::npos) {
                Logger::Warn(log_context) << "Admin user not found: user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::AdminUserNotFound));
            }

            Logger::Error(log_context)
                << "Admin get user detail database error: user_id=" << user_id
                << " - " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to get user detail"
            ));
        }
    }

    auto AdminService::ChangeUserStatus(
        uint64_t target_id,
        int status,
        uint64_t operator_id,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<void>> {

        Logger::Info(log_context) << "Admin change user status: target_id=" << target_id
                                  << " status=" << status << " operator_id=" << operator_id;

        if (target_id == operator_id) {
            Logger::Warn(log_context)
                << "Admin cannot modify self: operator_id=" << operator_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::AdminCannotModifySelf));
        }

        try {
            CoroMapper<Users> mapper(m_db_client);

            auto user = co_await mapper.findOne(
                Criteria(Users::Cols::_id, CompareOperator::EQ, target_id)
            );

            user.setStatus(static_cast<int8_t>(status));
            user.setLoginAttempts(0);
            user.setLockedUntilToNull();
            co_await mapper.update(user);

            Json::Value details;
            details["target_id"] = static_cast<Json::UInt64>(target_id);
            details["status"] = status;
            co_await LogOperation(
                operator_id,
                "admin.user.status_change",
                "user",
                target_id,
                user.getValueOfUsername(),
                std::move(details),
                log_context
            );

            Logger::Info(log_context)
                << "Admin change user status successful: target_id=" << target_id;
            co_return {};

        } catch (const drogon::orm::DrogonDbException& e) {
            const auto error_msg = std::string(e.base().what());
            if (error_msg.find("condition") != std::string::npos ||
                error_msg.find("empty") != std::string::npos) {
                Logger::Warn(log_context)
                    << "Admin user not found for status change: target_id=" << target_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::AdminUserNotFound));
            }

            Logger::Error(log_context)
                << "Admin change user status database error: " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to change user status"
            ));
        }
    }

    auto AdminService::ChangeUserRole(
        uint64_t target_id,
        int role,
        uint64_t operator_id,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<void>> {

        Logger::Info(log_context) << "Admin change user role: target_id=" << target_id
                                  << " role=" << role << " operator_id=" << operator_id;

        if (target_id == operator_id) {
            Logger::Warn(log_context)
                << "Admin cannot modify self: operator_id=" << operator_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::AdminCannotModifySelf));
        }

        try {
            CoroMapper<Users> mapper(m_db_client);

            auto user = co_await mapper.findOne(
                Criteria(Users::Cols::_id, CompareOperator::EQ, target_id)
            );

            if (role == 0 && user.getValueOfRole() == 1) {
                try {
                    auto count_result = co_await m_db_client->execSqlCoro(
                        "SELECT COUNT(*) AS cnt FROM users WHERE role = 1"
                    );
                    if (!count_result.empty()) {
                        int admin_count = count_result[0]["cnt"].as<int>();
                        if (admin_count <= 1) {
                            Logger::Warn(log_context)
                                << "Cannot demote last admin: target_id=" << target_id;
                            co_return std::unexpected(ErrorInfo(ErrorCode::AdminCannotDemoteLast));
                        }
                    }
                } catch (const drogon::orm::DrogonDbException& e) {
                    Logger::Error(log_context)
                        << "Failed to count admins: " << e.base().what();
                    co_return std::unexpected(ErrorInfo(
                        ErrorCode::InternalError,
                        "Failed to verify admin count"
                    ));
                }
            }

            user.setRole(static_cast<int8_t>(role));
            co_await mapper.update(user);

            Json::Value details;
            details["target_id"] = static_cast<Json::UInt64>(target_id);
            details["role"] = role;
            co_await LogOperation(
                operator_id,
                "admin.user.role_change",
                "user",
                target_id,
                user.getValueOfUsername(),
                std::move(details),
                log_context
            );

            Logger::Info(log_context)
                << "Admin change user role successful: target_id=" << target_id;
            co_return {};

        } catch (const drogon::orm::DrogonDbException& e) {
            const auto error_msg = std::string(e.base().what());
            if (error_msg.find("condition") != std::string::npos ||
                error_msg.find("empty") != std::string::npos) {
                Logger::Warn(log_context)
                    << "Admin user not found for role change: target_id=" << target_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::AdminUserNotFound));
            }

            Logger::Error(log_context)
                << "Admin change user role database error: " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to change user role"
            ));
        }
    }

    auto AdminService::ChangeUserAvailableSpace(
        uint64_t target_id,
        uint64_t available_space_g,
        uint64_t operator_id,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<admin::UserDetailResponse>> {

        Logger::Info(log_context)
            << "Admin change user available space: target_id=" << target_id
            << " available_space_g=" << available_space_g
            << " operator_id=" << operator_id;

        if (target_id == operator_id) {
            Logger::Warn(log_context)
                << "Admin cannot modify self available space: operator_id=" << operator_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::AdminCannotModifySelf));
        }

        try {
            CoroMapper<Users> mapper(m_db_client);

            auto user = co_await mapper.findOne(
                Criteria(Users::Cols::_id, CompareOperator::EQ, target_id)
            );

            constexpr uint64_t bytes_per_g = 1024ULL * 1024ULL * 1024ULL;
            const auto storage_used = user.getValueOfStorageUsed();
            const auto storage_reserved = user.getValueOfStorageReserved();
            const auto old_storage_quota = user.getValueOfStorageQuota();

            if (available_space_g > std::numeric_limits<uint64_t>::max() / bytes_per_g) {
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "Available space is too large"
                ));
            }

            const auto available_space_bytes = available_space_g * bytes_per_g;
            if (storage_used > std::numeric_limits<uint64_t>::max() - storage_reserved ||
                storage_used + storage_reserved > std::numeric_limits<uint64_t>::max() - available_space_bytes) {
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "Available space is too large"
                ));
            }

            const auto new_storage_quota = storage_used + storage_reserved + available_space_bytes;
            user.setStorageQuota(new_storage_quota);
            co_await mapper.update(user);

            Json::Value details;
            details["target_id"] = static_cast<Json::UInt64>(target_id);
            details["available_space_g"] = static_cast<Json::UInt64>(available_space_g);
            details["old_storage_quota"] = static_cast<Json::UInt64>(old_storage_quota);
            details["new_storage_quota"] = static_cast<Json::UInt64>(new_storage_quota);
            details["storage_used"] = static_cast<Json::UInt64>(storage_used);
            details["storage_reserved"] = static_cast<Json::UInt64>(storage_reserved);
            co_await LogOperation(
                operator_id,
                "admin.user.available_space_set",
                "user",
                target_id,
                user.getValueOfUsername(),
                std::move(details),
                log_context
            );

            admin::UserDetailResponse response;
            response.id = user.getValueOfId();
            response.username = user.getValueOfUsername();
            response.email = user.getValueOfEmail();
            response.nickname = user.getNickname() ? *user.getNickname() : "";
            response.avatar = user.getAvatar() ? *user.getAvatar() : "";
            response.role = user.getValueOfRole();
            response.status = user.getValueOfStatus();
            response.storage_quota = new_storage_quota;
            response.storage_used = storage_used;
            response.storage_reserved = storage_reserved;
            response.created_at = user.getValueOfCreatedAt().toDbStringLocal();
            response.last_login_at = user.getLastLoginAt() ? user.getValueOfLastLoginAt().toDbStringLocal() : "";

            Logger::Info(log_context)
                << "Admin change user available space successful: target_id=" << target_id;
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            const auto error_msg = std::string(e.base().what());
            if (error_msg.find("condition") != std::string::npos ||
                error_msg.find("empty") != std::string::npos) {
                Logger::Warn(log_context)
                    << "Admin user not found for available space change: target_id=" << target_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::AdminUserNotFound));
            }

            Logger::Error(log_context)
                << "Admin change user available space database error: " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to change user available space"
            ));
        }
    }

    auto AdminService::SoftDeleteUser(
        uint64_t target_id,
        uint64_t operator_id,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<void>> {

        Logger::Info(log_context) << "Admin soft delete user: target_id=" << target_id
                                  << " operator_id=" << operator_id;

        if (target_id == operator_id) {
            Logger::Warn(log_context)
                << "Admin cannot delete self: operator_id=" << operator_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::AdminCannotModifySelf));
        }

        try {
            CoroMapper<Users> mapper(m_db_client);

            auto user = co_await mapper.findOne(
                Criteria(Users::Cols::_id, CompareOperator::EQ, target_id)
            );

            user.setStatus(static_cast<int8_t>(0));
            co_await mapper.update(user);

            Json::Value details;
            details["target_id"] = static_cast<Json::UInt64>(target_id);
            details["username"] = user.getValueOfUsername();
            co_await LogOperation(
                operator_id,
                "admin.user.soft_delete",
                "user",
                target_id,
                user.getValueOfUsername(),
                std::move(details),
                log_context
            );

            Logger::Info(log_context)
                << "Admin soft delete user successful: target_id=" << target_id;
            co_return {};

        } catch (const drogon::orm::DrogonDbException& e) {
            const auto error_msg = std::string(e.base().what());
            if (error_msg.find("condition") != std::string::npos ||
                error_msg.find("empty") != std::string::npos) {
                Logger::Warn(log_context)
                    << "Admin user not found for soft delete: target_id=" << target_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::AdminUserNotFound));
            }

            Logger::Error(log_context)
                << "Admin soft delete user database error: " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to soft delete user"
            ));
        }
    }

    auto AdminService::GetGlobalStorageStats(
        uint64_t operator_id,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<admin::StorageStatsResponse>> {

        Logger::Info(log_context) << "Admin get global storage stats";

        try {
            auto user_stats = co_await m_db_client->execSqlCoro(
                "SELECT COUNT(*) AS total_users, " "COALESCE(SUM(storage_used), 0) AS total_storage_used, " "COALESCE(SUM(storage_quota), 0) AS total_storage_quota " "FROM users"
            );

            auto file_stats = co_await m_db_client->execSqlCoro(
                "SELECT COUNT(*) AS total_files FROM files"
            );

            auto share_stats = co_await m_db_client->execSqlCoro(
                "SELECT COUNT(*) AS active_shares FROM shares WHERE status = 1"
            );

            admin::StorageStatsResponse response;
            response.total_users = user_stats.empty() ? 0 : user_stats[0]["total_users"].as<int>();
            response.total_storage_used = user_stats.empty() ? 0 : user_stats[0]["total_storage_used"].as<uint64_t>();
            response.total_storage_quota = user_stats.empty() ? 0 : user_stats[0]["total_storage_quota"].as<uint64_t>();
            response.total_files = file_stats.empty() ? 0 : file_stats[0]["total_files"].as<int>();
            response.active_shares = share_stats.empty() ? 0 : share_stats[0]["active_shares"].as<int>();

            Json::Value details;
            details["total_users"] = response.total_users;
            details["total_files"] = response.total_files;
            details["total_storage_used"] =
                static_cast<Json::UInt64>(response.total_storage_used);
            details["active_shares"] = response.active_shares;
            co_await LogOperation(
                operator_id,
                "admin.storage.global_stats",
                "storage",
                0,
                "全局存储统计",
                std::move(details),
                log_context
            );

            Logger::Info(log_context) << "Admin get global storage stats successful";
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error(log_context)
                << "Admin get global storage stats database error: " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to get global storage stats"
            ));
        }
    }

    auto AdminService::ListShares(
        const admin::ListSharesRequest& req,
        uint64_t operator_id,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<admin::ShareListResponse>> {

        Logger::Info(log_context) << "Admin list shares: page=" << req.page
                                  << " page_size=" << req.page_size;

        try {
            co_await m_db_client->execSqlCoro(
                "UPDATE shares AS s SET status = 0, updated_at = NOW() " "WHERE s.status = 1 " "AND NOT EXISTS (SELECT 1 FROM share_files sf WHERE sf.share_id = s.id)"
            );

            std::string from_clause = " FROM shares s LEFT JOIN users u ON s.user_id = u.id";
            std::string where_clause = " WHERE 1=1";
            if (req.status.has_value()) {
                where_clause += " AND s.status = " + std::to_string(*req.status);
            }
            if (req.user_id.has_value()) {
                where_clause += " AND s.user_id = " + std::to_string(*req.user_id);
            }
            auto username_like = req.username.has_value() ? "%" + *req.username + "%" : std::string{};
            const size_t like_index = 1;
            const size_t limit_index = req.username.has_value() ? 2 : 1;
            const size_t offset_index = req.username.has_value() ? 3 : 2;
            if (req.username.has_value()) {
                where_clause += " AND u.username LIKE $" + std::to_string(like_index);
            }

            auto count_result = req.username.has_value() ? co_await m_db_client->execSqlCoro(
                                                               "SELECT COUNT(*) AS total" + from_clause + where_clause,
                                                               username_like
                                                           ) :
                                                           co_await m_db_client->execSqlCoro(
                                                               "SELECT COUNT(*) AS total" + from_clause + where_clause
                                                           );

            int total = 0;
            if (!count_result.empty()) {
                total = count_result[0]["total"].as<int>();
            }

            int offset = (req.page - 1) * req.page_size;
            int total_pages = req.page_size > 0 ? static_cast<int>(std::ceil(static_cast<double>(total) / req.page_size)) : 0;

            auto limit_offset = " ORDER BY s.created_at DESC LIMIT $" + std::to_string(limit_index) + " OFFSET $" + std::to_string(offset_index);
            auto query_sql =
                "SELECT s.id, s.user_id, u.username, sf.item_id AS file_id, f.name AS file_name, " "s.share_code, s.status, " "(s.view_count + s.download_count) AS access_count, " "(s.password_hash IS NOT NULL) AS password_set, " "s.created_at, s.expires_at " + from_clause + " " "LEFT JOIN share_files sf ON sf.id = (" "    SELECT MIN(sf2.id) " "    FROM share_files sf2 " "    WHERE sf2.share_id = s.id AND sf2.item_type = 'file'" ") " "LEFT JOIN files f ON sf.item_id = f.id " + where_clause + limit_offset;
            auto result = req.username.has_value() ? co_await m_db_client->execSqlCoro(
                                                         query_sql,
                                                         username_like,
                                                         req.page_size,
                                                         offset
                                                     ) :
                                                     co_await m_db_client->execSqlCoro(
                                                         query_sql,
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
                share.password_set = !row["password_set"].isNull() && row["password_set"].as<int>() != 0;
                share.created_at = row["created_at"].as<std::string>();
                share.expires_at = row["expires_at"].isNull() ? "" : row["expires_at"].as<std::string>();
                response.items.push_back(std::move(share));
            }

            Json::Value details;
            details["page"] = req.page;
            details["page_size"] = req.page_size;
            details["total"] = total;
            co_await LogOperation(
                operator_id,
                "admin.share.list",
                "share",
                0,
                "分享列表",
                std::move(details),
                log_context
            );

            Logger::Info(log_context) << "Admin list shares successful: total=" << total;
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error(log_context)
                << "Admin list shares database error: " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to list shares"
            ));
        }
    }

    auto AdminService::GetShareDetail(
        uint64_t share_id,
        uint64_t operator_id,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<admin::ShareDetailResponse>> {

        Logger::Info(log_context) << "Admin get share detail: share_id=" << share_id;

        try {
            co_await m_db_client->execSqlCoro(
                "UPDATE shares AS s SET status = 0, updated_at = NOW() " "WHERE s.id = $1 AND s.status = 1 " "AND NOT EXISTS (SELECT 1 FROM share_files sf WHERE sf.share_id = s.id)",
                share_id
            );

            auto result = co_await m_db_client->execSqlCoro(
                "SELECT s.id, s.user_id, u.username, sf.item_id AS file_id, f.name AS file_name, " "s.share_code, s.status, " "(s.view_count + s.download_count) AS access_count, " "(s.password_hash IS NOT NULL) AS password_set, " "s.created_at, s.expires_at " "FROM shares s " "LEFT JOIN users u ON s.user_id = u.id " "LEFT JOIN share_files sf ON sf.id = (" "    SELECT MIN(sf2.id) " "    FROM share_files sf2 " "    WHERE sf2.share_id = s.id AND sf2.item_type = 'file'" ") " "LEFT JOIN files f ON sf.item_id = f.id " "WHERE s.id = $1",
                share_id
            );

            if (result.empty()) {
                Logger::Warn(log_context)
                    << "Admin share not found: share_id=" << share_id;
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
            response.password_set = !row["password_set"].isNull() && row["password_set"].as<int>() != 0;
            response.created_at = row["created_at"].as<std::string>();
            response.expires_at = row["expires_at"].isNull() ? "" : row["expires_at"].as<std::string>();

            Json::Value details;
            details["share_id"] = static_cast<Json::UInt64>(share_id);
            co_await LogOperation(
                operator_id,
                "admin.share.detail",
                "share",
                share_id,
                response.share_code,
                std::move(details),
                log_context
            );

            Logger::Info(log_context)
                << "Admin get share detail successful: share_id=" << share_id;
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error(log_context)
                << "Admin get share detail database error: share_id=" << share_id
                << " - " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to get share detail"
            ));
        }
    }

    auto AdminService::ForceCancelShare(
        uint64_t share_id,
        uint64_t operator_id,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<void>> {

        Logger::Info(log_context) << "Admin force cancel share: share_id=" << share_id
                                  << " operator_id=" << operator_id;

        try {
            auto result = co_await m_db_client->execSqlCoro(
                "SELECT id, share_code, user_id, status FROM shares WHERE id = $1",
                share_id
            );

            if (result.empty()) {
                Logger::Warn(log_context)
                    << "Admin share not found for force cancel: share_id=" << share_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::AdminShareNotFound));
            }

            auto current_status = result[0]["status"].as<int>();
            if (current_status == 0) {
                Logger::Warn(log_context)
                    << "Admin share already cancelled: share_id=" << share_id;
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "Share already cancelled"
                ));
            }

            co_await m_db_client->execSqlCoro(
                "UPDATE shares SET status = 0, updated_at = NOW() WHERE id = $1",
                share_id
            );

            auto share_code = result[0]["share_code"].as<std::string>();
            auto owner_id = result[0]["user_id"].as<uint64_t>();
            Json::Value details;
            details["share_id"] = static_cast<Json::UInt64>(share_id);
            details["share_code"] = share_code;
            details["owner_id"] = static_cast<Json::UInt64>(owner_id);
            details["previous_status"] = current_status;
            co_await LogOperation(
                operator_id,
                "admin.share.force_cancel",
                "share",
                share_id,
                share_code,
                std::move(details),
                log_context
            );

            Logger::Info(log_context)
                << "Admin force cancel share successful: share_id=" << share_id;
            co_return {};

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error(log_context)
                << "Admin force cancel share database error: " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to force cancel share"
            ));
        }
    }

    auto AdminService::GetOverviewStats(disk::utils::LogContext log_context)
        -> drogon::Task<Result<admin::StorageStatsResponse>> {

        Logger::Info(log_context) << "admin.stats.overview";

        try {
            auto user_stats = co_await m_db_client->execSqlCoro(
                "SELECT COUNT(*) AS total_users, " "COALESCE(SUM(storage_used), 0) AS total_storage_used, " "COALESCE(SUM(storage_quota), 0) AS total_storage_quota " "FROM users"
            );

            auto file_stats = co_await m_db_client->execSqlCoro(
                "SELECT COUNT(*) AS total_files FROM files"
            );

            auto share_stats = co_await m_db_client->execSqlCoro(
                "SELECT COUNT(*) AS active_shares FROM shares WHERE status = 1"
            );

            admin::StorageStatsResponse response;
            response.total_users = user_stats.empty() ? 0 : user_stats[0]["total_users"].as<int>();
            response.total_storage_used = user_stats.empty() ? 0 : user_stats[0]["total_storage_used"].as<uint64_t>();
            response.total_storage_quota = user_stats.empty() ? 0 : user_stats[0]["total_storage_quota"].as<uint64_t>();
            response.total_files = file_stats.empty() ? 0 : file_stats[0]["total_files"].as<int>();
            response.active_shares = share_stats.empty() ? 0 : share_stats[0]["active_shares"].as<int>();

            Logger::Info(log_context) << "admin.stats.overview successful";
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error(log_context)
                << "admin.stats.overview database error: " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to get overview stats"
            ));
        }
    }

    auto AdminService::GetSystemStatus(disk::utils::LogContext log_context)
        -> drogon::Task<Result<admin::SystemStatusResponse>> {

        Logger::Info(log_context) << "admin.stats.system";

        admin::SystemStatusResponse response;

        /// Database check
        try {
            co_await m_db_client->execSqlCoro("SELECT 1");
            response.db_connected = true;
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn(log_context)
                << "admin.stats.system Database check failed: " << e.base().what();
            response.db_connected = false;
        }

        /// Redis check
        try {
            auto redis_client = drogon::app().getRedisClient();
            if (redis_client) {
                RedisService::Initialize(redis_client);
                const auto result =
                    co_await RedisService::GetInstance()->Ping(log_context);
                response.redis_connected = result.has_value() && *result;
            } else {
                response.redis_connected = false;
            }
        } catch (const drogon::nosql::RedisException& e) {
            Logger::Warn(log_context)
                << "admin.stats.system Redis check failed: " << e.what();
            response.redis_connected = false;
        } catch (const std::exception& e) {
            Logger::Warn(log_context)
                << "admin.stats.system Redis check failed: " << e.what();
            response.redis_connected = false;
        }

        /// Disk space
        try {
            auto storage_path = utils::ConfigMgr::GetInstance()->GetStorageBasePath();
            auto space_info = std::filesystem::space(storage_path);
            response.disk_total = space_info.capacity;
            response.disk_free = space_info.available;
            response.disk_used = space_info.capacity - space_info.available;
        } catch (const std::filesystem::filesystem_error& e) {
            Logger::Warn(log_context)
                << "admin.stats.system disk space check failed: " << e.what();
            response.disk_total = 0;
            response.disk_used = 0;
            response.disk_free = 0;
        }

        /// Uptime
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - m_start_time);
        response.uptime_seconds = static_cast<uint64_t>(uptime.count());

        Logger::Info(log_context) << "admin.stats.system successful";
        co_return response;
    }

    auto AdminService::GetAdminLogs(
        const admin::AdminLogListRequest& req,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<admin::AdminLogListResponse>> {

        Logger::Info(log_context) << "Admin list logs: page=" << req.page
                                  << " page_size=" << req.page_size;

        try {
            static constexpr auto FILTER_SQL = R"SQL(
 WHERE ($1::boolean = FALSE OR action = $2)
   AND ($3::boolean = FALSE OR created_at >= $4::date)
   AND ($5::boolean = FALSE OR created_at < $6::date + INTERVAL '1 day')
   AND ($7::boolean = FALSE OR target_type = $8)
   AND ($9::boolean = FALSE OR target_name = $10)
)SQL";
            const auto action = req.action.value_or("");
            const auto start_date = req.start_date.value_or("1970-01-01");
            const auto end_date = req.end_date.value_or("1970-01-01");
            const auto target_type = req.target_type.value_or("");
            const auto target_name = req.target_name.value_or("");

            auto count_result = co_await m_db_client->execSqlCoro(
                std::string("SELECT COUNT(*) AS total FROM operation_logs") + FILTER_SQL,
                req.action.has_value(),
                action,
                req.start_date.has_value(),
                start_date,
                req.end_date.has_value(),
                end_date,
                req.target_type.has_value(),
                target_type,
                req.target_name.has_value(),
                target_name
            );

            int total = 0;
            if (!count_result.empty()) {
                total = count_result[0]["total"].as<int>();
            }

            const auto offset =
                static_cast<int64_t>(req.page - 1) * static_cast<int64_t>(req.page_size);
            int total_pages = req.page_size > 0 ? static_cast<int>(std::ceil(static_cast<double>(total) / req.page_size)) : 0;

            auto result = co_await m_db_client->execSqlCoro(
                std::string("SELECT id, user_id, action, target_type, target_id, target_name, details, ip_address, created_at FROM operation_logs") + FILTER_SQL +
                    " ORDER BY created_at DESC, id DESC LIMIT $11 OFFSET $12",
                req.action.has_value(),
                action,
                req.start_date.has_value(),
                start_date,
                req.end_date.has_value(),
                end_date,
                req.target_type.has_value(),
                target_type,
                req.target_name.has_value(),
                target_name,
                static_cast<int64_t>(req.page_size),
                offset
            );

            admin::AdminLogListResponse response;
            response.pagination.page = req.page;
            response.pagination.page_size = req.page_size;
            response.pagination.total = total;
            response.pagination.total_pages = total_pages;

            for (const auto& row : result) {
                admin::AdminLogDetailResponse log;
                log.id = row["id"].as<uint64_t>();
                log.user_id = row["user_id"].isNull() ? std::optional<uint64_t>{} : std::optional<uint64_t>{ row["user_id"].as<uint64_t>() };
                log.action = row["action"].as<std::string>();
                log.target_type = row["target_type"].isNull() ? "" : row["target_type"].as<std::string>();
                log.target_id = row["target_id"].isNull() ? std::optional<uint64_t>{} : std::optional<uint64_t>{ row["target_id"].as<uint64_t>() };
                log.target_name = row["target_name"].isNull() ? std::optional<std::string>{} : std::optional<std::string>{ row["target_name"].as<std::string>() };
                log.details = row["details"].isNull() ? std::optional<std::string>{} : std::optional<std::string>{ row["details"].as<std::string>() };
                log.ip_address = row["ip_address"].as<std::string>();
                log.created_at = row["created_at"].as<std::string>();
                response.items.push_back(std::move(log));
            }

            Logger::Info(log_context) << "Admin list logs successful: total=" << total;
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error(log_context)
                << "Admin list logs database error: " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to list operation logs"
            ));
        }
    }

    auto AdminService::LogOperation(
        uint64_t operator_id,
        const std::string& action,
        const std::string& target_type,
        uint64_t target_id,
        const std::string& target_name,
        Json::Value details,
        disk::utils::LogContext log_context
    ) -> drogon::Task<void> {
        SetLogContext(details, log_context);
        try {
            co_await m_db_client->execSqlCoro(
                "INSERT INTO operation_logs (user_id, action, target_type, target_id, target_name, details, ip_address) " "VALUES ($1, $2, $3, $4, $5, $6, 'system')",
                operator_id,
                action,
                target_type,
                target_id,
                target_name,
                SerializeDetails(details)
            );
            Logger::Debug(log_context)
                << "Operation logged: " << action << " by user_id=" << operator_id;
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error(log_context) << "Failed to log operation: " << e.base().what();
        }
    }

} // namespace disk::services
