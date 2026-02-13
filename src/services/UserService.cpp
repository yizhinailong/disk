/**
 * @file UserService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 用户服务实现
 * @version 0.1
 * @date 2026-01-28
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "UserService.hpp"

#include <drogon/orm/CoroMapper.h>
#include <drogon/orm/Criteria.h>

#include "utils/HashUtil.hpp"

namespace disk::user {

    using disk::utils::HashUtil;
    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::Files;
    using drogon_model::disk::Folders;
    using drogon_model::disk::Users;

    UserService::UserService(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
        LOG_DEBUG << "UserService 初始化完成";
    }

    auto UserService::GetProfile(uint64_t user_id) -> drogon::Task<Result<UserProfileResponse>> {
        LOG_INFO << "获取用户信息请求: user_id=" << user_id;

        try {
            // Step 1: 查询用户信息
            CoroMapper<Users> user_mapper(m_db_client);
            const auto user = co_await user_mapper.findOne(
                Criteria(Users::Cols::_id, CompareOperator::EQ, user_id)
            );
            LOG_DEBUG << "查询到用户: " << user.getValueOfUsername() << " (ID: " << user_id << ")";

            // Step 2: 检查用户是否存在（findOne 会抛出异常如果不存在）
            // 不需要额外检查，因为 findOne 会抛出 DrogonDbException

            // Step 3: 查询文件数量
            CoroMapper<Files> file_mapper(m_db_client);
            const auto file_count = co_await file_mapper.count(
                Criteria(Files::Cols::_user_id, CompareOperator::EQ, user_id)
            );
            LOG_DEBUG << "用户文件数量: user_id=" << user_id << ", count=" << file_count;

            // Step 4: 查询文件夹数量
            CoroMapper<Folders> folder_mapper(m_db_client);
            const auto folder_count = co_await folder_mapper.count(
                Criteria(Folders::Cols::_user_id, CompareOperator::EQ, user_id)
            );
            LOG_DEBUG << "用户文件夹数量: user_id=" << user_id << ", count=" << folder_count;

            // Step 5: 构建 UserProfileResponse
            UserProfileResponse response;

            // Copy 基本字段
            response.id = user.getValueOfId();
            response.username = user.getValueOfUsername();
            response.email = user.getValueOfEmail();

            // Handle nullable nickname
            response.nickname = user.getNickname() ? *user.getNickname() : "";

            // Handle nullable avatar
            response.avatar = user.getAvatar() ? *user.getAvatar() : "";

            // Copy 存储信息
            response.storage_quota = user.getValueOfStorageQuota();
            response.storage_used = user.getValueOfStorageUsed();

            // 添加统计信息
            response.file_count = static_cast<uint32_t>(file_count);
            response.folder_count = static_cast<uint32_t>(folder_count);

            // Format 时间戳
            response.created_at = user.getValueOfCreatedAt().toDbStringLocal();
            response.updated_at = user.getValueOfUpdatedAt().toDbStringLocal();

            // Step 6: 返回响应
            LOG_INFO << "获取用户信息成功: user_id=" << user_id;
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            // 检查是否是"未找到"错误（drogon 抛出异常表示记录不存在）
            const auto error_msg = std::string(e.base().what());
            if (error_msg.find("condition") != std::string::npos ||
                error_msg.find("empty") != std::string::npos) {
                LOG_WARN << "用户不存在: user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::UserNotFound));
            }

            // 其他数据库错误
            LOG_ERROR << "获取用户信息数据库错误: user_id=" << user_id << " - " << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "获取用户信息失败，请稍后重试"));
        } catch (const std::exception& e) {
            LOG_ERROR << "获取用户信息未知错误: user_id=" << user_id << " - " << e.what();
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "获取用户信息失败，请稍后重试"));
        }
    }

    auto UserService::ChangePassword(uint64_t user_id, ChangePasswordRequest request)
        -> drogon::Task<Result<void>> {

        LOG_INFO << "修改密码请求: user_id=" << user_id;

        try {
            CoroMapper<Users> mapper(m_db_client);

            // Step 1: 查找用户
            auto user = co_await mapper.findOne(
                Criteria(Users::Cols::_id, CompareOperator::EQ, user_id)
            );
            LOG_DEBUG << "查询到用户: " << user.getValueOfUsername() << " (ID: " << user_id << ")";

            // Step 2: 验证旧密码
            if (!HashUtil::VerifyPassword(request.old_password, user.getValueOfPasswordHash())) {
                LOG_WARN << "旧密码错误: user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::InvalidCredentials));
            }
            LOG_DEBUG << "旧密码验证成功: user_id=" << user_id;

            // Step 3: 拒绝与当前密码相同的密码
            if (request.old_password == request.new_password) {
                LOG_WARN << "新密码不能与旧密码相同: user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "新密码不能与旧密码相同"));
            }

            // Step 4: 加密新密码
            LOG_DEBUG << "开始密码哈希计算: user_id=" << user_id;
            auto hash_result = HashUtil::HashPassword(request.new_password);
            if (!hash_result) {
                LOG_ERROR << "密码哈希失败: user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "密码加密失败，请稍后重试"));
            }
            LOG_DEBUG << "密码哈希完成: user_id=" << user_id;

            // Step 5: 更新数据库
            user.setPasswordHash(hash_result.value());
            co_await mapper.update(user);

            LOG_INFO << "密码修改成功: user_id=" << user_id;
            co_return {};

        } catch (const drogon::orm::DrogonDbException& e) {
            const auto error_msg = std::string(e.base().what());
            if (error_msg.find("condition") != std::string::npos ||
                error_msg.find("empty") != std::string::npos) {
                LOG_WARN << "用户不存在: user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::UserNotFound));
            }

            LOG_ERROR << "密码修改数据库错误: user_id=" << user_id << " - " << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "密码修改失败，请稍后重试"));
        } catch (const std::exception& e) {
            LOG_ERROR << "密码修改未知错误: user_id=" << user_id << " - " << e.what();
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "密码修改失败，请稍后重试"));
        }
    }

    auto UserService::UpdateProfile(uint64_t user_id, UpdateProfileRequest request)
        -> drogon::Task<Result<UserProfileResponse>> {

        LOG_INFO << "更新用户资料请求: user_id=" << user_id;

        try {
            CoroMapper<Users> mapper(m_db_client);

            // Step 1: 查找用户
            auto user = co_await mapper.findOne(
                Criteria(Users::Cols::_id, CompareOperator::EQ, user_id)
            );
            LOG_DEBUG << "查询到用户: " << user.getValueOfUsername();

            // Step 2: 更新提供的字段
            if (request.nickname.has_value()) {
                user.setNickname(*request.nickname);
                LOG_DEBUG << "更新昵称: " << *request.nickname;
            }
            if (request.avatar.has_value()) {
                user.setAvatar(*request.avatar);
                LOG_DEBUG << "更新头像: " << *request.avatar;
            }

            // Step 3: 保存到数据库
            co_await mapper.update(user);
            LOG_INFO << "用户资料更新成功: user_id=" << user_id;

            // Step 4: 构建响应
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
                LOG_WARN << "用户不存在: user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::UserNotFound));
            }

            LOG_ERROR << "更新用户资料数据库错误: user_id=" << user_id << " - " << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "更新用户资料失败，请稍后重试"));
        } catch (const std::exception& e) {
            LOG_ERROR << "更新用户资料未知错误: user_id=" << user_id << " - " << e.what();
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "更新用户资料失败，请稍后重试"));
        }
    }

} // namespace disk::user
