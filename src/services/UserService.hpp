/**
 * @file UserService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 用户服务
 * @version 0.1
 * @date 2026-01-28
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 提供用户信息管理相关功能：
 * - 获取用户完整信息（包括统计信息）
 * - 文件数量统计
 * - 文件夹数量统计
 * - 存储空间使用情况
 */

#pragma once

#include <cstdint>

#include <drogon/orm/DbClient.h>

#include "dtos/UserDto.hpp"
#include "models/Files.hpp"
#include "models/Folders.hpp"
#include "models/Users.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::user {

    /**
     * @brief 用户服务类
     *
     * @details
     * 提供用户信息查询和统计功能：
     * - 获取用户完整信息（用户基本信息 + 统计数据）
     * - 查询用户文件数量
     * - 查询用户文件夹数量
     * - 查询存储空间使用情况
     *
     * 业务规则：
     * - 根据 user_id 查询 Users 表获取用户基本信息
     * - 根据 user_id 查询 Files 表统计文件数量（COUNT(*)）
     * - 根据 user_id 查询 Folders 表统计文件夹数量（COUNT(*)）
     * - 用户不存在时返回 UserNotFound 错误
     * - 数据库异常时记录错误日志并返回 InternalError
     */
    class UserService {
    public:
        /**
         * @brief 构造函数
         * @param db_client 数据库客户端
         */
        explicit UserService(drogon::orm::DbClientPtr db_client);
        ~UserService() = default;
        UserService(const UserService&) = delete;
        auto operator=(const UserService&) -> UserService& = delete;
        UserService(UserService&&) = default;
        auto operator=(UserService&&) -> UserService& = default;

        /**
         * @brief 获取用户信息
         *
         * 业务规则：
         * - 根据 user_id 查询 Users 表获取用户基本信息
         * - 根据 user_id 查询 Files 表统计文件数量
         * - 根据 user_id 查询 Folders 表统计文件夹数量
         * - 用户不存在时返回 UserNotFound 错误
         * - 数据库异常时记录错误日志并返回 InternalError
         *
         * @param user_id 用户 ID
         * @return drogon::Task<Result<UserProfileResponse>> 成功返回用户信息，失败返回错误
         */
        [[nodiscard]]
        auto GetProfile(uint64_t user_id) -> drogon::Task<Result<UserProfileResponse>>;

        /**
         * @brief 修改用户密码
         *
         * 业务规则：
         * - 根据 user_id 查询 Users 表获取用户
         * - 验证旧密码是否正确（与存储的哈希对比）
         * - 新密码不能与旧密码相同
         * - 使用 Argon2id 算法加密新密码
         * - 更新 Users 表的 password_hash 字段
         *
         * 错误处理：
         * - 用户不存在：UserNotFound (40100)
         * - 旧密码错误：InvalidCredentials (40101)
         * - 新密码与旧密码相同：ValidationFailed (10002)
         * - 数据库/哈希异常：InternalError (10006)
         *
         * @param user_id 用户 ID
         * @param request 修改密码请求（包含 old_password 和 new_password）
         * @return drogon::Task<VoidResult> 成功返回空，失败返回错误
         */
        [[nodiscard]]
        auto ChangePassword(uint64_t user_id, ChangePasswordRequest request)
            -> drogon::Task<VoidResult>;

    private:
        drogon::orm::DbClientPtr m_db_client; ///< 数据库客户端
    };

} // namespace disk::user
