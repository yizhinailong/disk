/**
 * @file UserService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 用户服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "UserService.hpp"

#include <cmath>

#include <drogon/orm/CoroMapper.h>
#include <drogon/orm/Criteria.h>

#include "utils/HashUtil.hpp"

namespace disk::user {

    using disk::utils::HashUtil;
    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::Users;

    UserService::UserService(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
        LOG_DEBUG << "UserService initialization completed";
    }

    auto UserService::GetProfile(uint64_t user_id) -> drogon::Task<Result<UserProfileResponse>> {
        LOG_INFO << "Get user profile request: user_id=" << user_id;

        try {
            // 单次聚合查询：用户基本信息 + 文件数量 + 文件夹数量
            auto result = co_await m_db_client->execSqlCoro(
                "SELECT u.id, u.username, u.email, u.nickname, u.avatar, " "       u.storage_quota, u.storage_used, " "       u.created_at, u.updated_at, " "       (SELECT COUNT(*) FROM files WHERE user_id = u.id) AS file_count, " "       (SELECT COUNT(*) FROM folders WHERE user_id = u.id) AS folder_count " "FROM users u " "WHERE u.id = ?",
                user_id
            );

            if (result.empty()) {
                LOG_WARN << "User not found: user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::UserNotFound));
            }

            const auto& row = result[0];
            LOG_DEBUG << "Found user: " << row["username"].as<std::string>() << " (ID: " << user_id
                      << ")";

            UserProfileResponse response;
            response.id = row["id"].as<uint64_t>();
            response.username = row["username"].as<std::string>();
            response.email = row["email"].as<std::string>();

            // 处理可空 nickname
            if (!row["nickname"].isNull()) {
                response.nickname = row["nickname"].as<std::string>();
            }

            // 处理可空 avatar
            if (!row["avatar"].isNull()) {
                response.avatar = row["avatar"].as<std::string>();
            }

            // 复制存储信息
            response.storage_quota = row["storage_quota"].as<uint64_t>();
            response.storage_used = row["storage_used"].as<uint64_t>();

            response.file_count = row["file_count"].as<uint32_t>();
            response.folder_count = row["folder_count"].as<uint32_t>();

            // 格式化时间戳
            response.created_at = row["created_at"].as<std::string>();
            response.updated_at = row["updated_at"].as<std::string>();

            LOG_INFO << "Get user profile successful: user_id=" << user_id;
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Get user profile database error: user_id=" << user_id << " - "
                      << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to get user profile, please try again later"
            ));
        } catch (const std::exception& e) {
            LOG_ERROR << "Get user profile unknown error: user_id=" << user_id << " - " << e.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to get user profile, please try again later"
            ));
        }
    }

    auto UserService::ChangePassword(uint64_t user_id, ChangePasswordRequest request)
        -> drogon::Task<Result<void>> {

        LOG_INFO << "Change password request: user_id=" << user_id;

        try {
            CoroMapper<Users> mapper(m_db_client);

            // 步骤 1: 查找用户
            auto user =
                co_await mapper.findOne(Criteria(Users::Cols::_id, CompareOperator::EQ, user_id));
            LOG_DEBUG << "Found user: " << user.getValueOfUsername() << " (ID: " << user_id << ")";

            // 步骤 2: 验证旧密码
            if (!HashUtil::VerifyPassword(request.old_password, user.getValueOfPasswordHash())) {
                LOG_WARN << "Old password is incorrect: user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::InvalidCredentials));
            }
            LOG_DEBUG << "Old password verified successfully: user_id=" << user_id;

            // 步骤 3: 拒绝与当前密码相同的密码
            if (request.old_password == request.new_password) {
                LOG_WARN << "New password cannot be the same as old password: user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "New password cannot be the same as old password"
                ));
            }

            // 步骤 4: 加密新密码
            LOG_DEBUG << "Starting password hash computation: user_id=" << user_id;
            auto hash_result = HashUtil::HashPassword(request.new_password);
            if (!hash_result) {
                LOG_ERROR << "Password hash failed: user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::InternalError,
                    "Password encryption failed, please try again later"
                ));
            }
            LOG_DEBUG << "Password hash completed: user_id=" << user_id;

            // 步骤 5: 更新数据库
            user.setPasswordHash(hash_result.value());
            co_await mapper.update(user);

            LOG_INFO << "Password changed successfully: user_id=" << user_id;
            co_return {};

        } catch (const drogon::orm::DrogonDbException& e) {
            const auto error_msg = std::string(e.base().what());
            if (error_msg.find("condition") != std::string::npos ||
                error_msg.find("empty") != std::string::npos) {
                LOG_WARN << "User not found: user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::UserNotFound));
            }

            LOG_ERROR << "Change password database error: user_id=" << user_id << " - "
                      << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to change password, please try again later"
            ));
        } catch (const std::exception& e) {
            LOG_ERROR << "Change password unknown error: user_id=" << user_id << " - " << e.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to change password, please try again later"
            ));
        }
    }

    auto UserService::UpdateProfile(uint64_t user_id, UpdateProfileRequest request)
        -> drogon::Task<Result<UserProfileResponse>> {

        LOG_INFO << "Update user profile request: user_id=" << user_id;

        try {
            CoroMapper<Users> mapper(m_db_client);

            // 步骤 1: 查找用户
            auto user =
                co_await mapper.findOne(Criteria(Users::Cols::_id, CompareOperator::EQ, user_id));
            LOG_DEBUG << "Found user: " << user.getValueOfUsername();

            // 步骤 2: 更新提供的字段
            if (request.nickname.has_value()) {
                user.setNickname(*request.nickname);
                LOG_DEBUG << "Updating nickname: " << *request.nickname;
            }
            if (request.avatar.has_value()) {
                user.setAvatar(*request.avatar);
                LOG_DEBUG << "Updating avatar: " << *request.avatar;
            }

            // 步骤 3: 保存到数据库
            co_await mapper.update(user);
            LOG_INFO << "User profile updated successfully: user_id=" << user_id;

            // 步骤 4: 构建响应
            UserProfileResponse response;
            response.id = user.getValueOfId();
            response.username = user.getValueOfUsername();
            response.email = user.getValueOfEmail();
            response.nickname = user.getNickname() ? *user.getNickname() : "";
            response.avatar = user.getAvatar() ? *user.getAvatar() : "";
            response.storage_quota = user.getValueOfStorageQuota();
            response.storage_used = user.getValueOfStorageUsed();
            response.file_count = 0; // 下次 GetProfile 时会准确
            response.folder_count = 0;
            response.created_at = user.getValueOfCreatedAt().toDbStringLocal();
            response.updated_at = user.getValueOfUpdatedAt().toDbStringLocal();

            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            const auto error_msg = std::string(e.base().what());
            if (error_msg.find("condition") != std::string::npos ||
                error_msg.find("empty") != std::string::npos) {
                LOG_WARN << "User not found: user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::UserNotFound));
            }

            LOG_ERROR << "Update user profile database error: user_id=" << user_id << " - "
                      << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to update user profile, please try again later"
            ));
        } catch (const std::exception& e) {
            LOG_ERROR << "Update user profile unknown error: user_id=" << user_id << " - "
                      << e.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to update user profile, please try again later"
            ));
        }
    }

    auto UserService::GetStorage(uint64_t user_id) -> drogon::Task<Result<StorageResponse>> {
        LOG_DEBUG << "Get user storage stats: user_id=" << user_id;

        try {
            // 单次聚合查询：配额 + 已使用 + 文件数量 + 文件夹数量
            auto result = co_await m_db_client->execSqlCoro(
                "SELECT u.storage_quota, " "       COALESCE((SELECT SUM(f.size) FROM files f WHERE f.user_id = u.id), 0) AS used, " "       (SELECT COUNT(*) FROM files WHERE user_id = u.id) AS file_count, " "       (SELECT COUNT(*) FROM folders WHERE user_id = u.id) AS folder_count " "FROM users u " "WHERE u.id = ?",
                user_id
            );

            if (result.empty()) {
                LOG_ERROR << "User not found for storage stats: user_id=" << user_id;
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to get storage stats")
                );
            }

            const auto& row = result[0];
            uint64_t quota = row["storage_quota"].as<uint64_t>();
            uint64_t used = row["used"].as<uint64_t>();
            uint32_t file_count = row["file_count"].as<uint32_t>();
            uint32_t folder_count = row["folder_count"].as<uint32_t>();

            // 百分比（1位小数）
            double percentage = 0.0;
            if (quota > 0) {
                percentage =
                    std::round(static_cast<double>(used) / static_cast<double>(quota) * 1000.0) /
                    10.0;
            }

            StorageResponse response{ .used = used,
                                      .quota = quota,
                                      .percentage = percentage,
                                      .file_count = file_count,
                                      .folder_count = folder_count,
                                      .categories = {} };

            LOG_DEBUG << "Storage stats: used=" << used << ", quota=" << quota
                      << ", percentage=" << percentage << "%"
                      << ", files=" << file_count << ", folders=" << folder_count;

            co_return response;
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to get storage stats: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to get storage stats")
            );
        }
    }

} // namespace disk::user
