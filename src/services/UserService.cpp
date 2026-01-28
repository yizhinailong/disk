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

namespace disk::user {

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

} // namespace disk::user
