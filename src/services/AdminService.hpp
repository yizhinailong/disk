/**
 * @file AdminService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 管理员用户管理服务
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 提供管理员用户管理功能：
 * - 获取用户列表（分页、筛选）
 * - 获取用户详情
 * - 修改用户状态
 * - 修改用户角色
 * - 软删除用户
 *
 * 安全规则：
 * - 管理员不能修改自身状态/角色（AdminCannotModifySelf）
 * - 不能降级最后一个管理员（AdminCannotDemoteLast）
 * - 所有写操作记录操作日志（admin.user.* 前缀）
 */

#pragma once

#include <chrono>
#include <cstdint>

#include <drogon/orm/DbClient.h>

#include "dtos/AdminDto.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/Singleton.hpp"

namespace disk::services {

    /**
     * @brief 管理员用户管理服务
     *
     * @details
     * 提供管理员对用户的管理功能，包括列表查询、详情查看、
     * 状态修改、角色变更和软删除操作。
     */
    class AdminService : public utils::Singleton<AdminService> {
        friend class utils::Singleton<AdminService>;

    public:
        /**
         * @brief 获取用户列表
         *
         * 业务规则：
         * - 支持按用户名（LIKE）、邮箱（LIKE）、状态、角色筛选
         * - 按 created_at DESC 排序
         * - 支持分页，自动计算 total_pages
         *
         * @param req 列表请求参数
         * @return drogon::Task<Result<admin::UserListResponse>> 用户列表响应
         */
        [[nodiscard]]
        auto ListUsers(const admin::ListUsersRequest& req)
            -> drogon::Task<Result<admin::UserListResponse>>;

        /**
         * @brief 获取用户详情
         *
         * @param user_id 用户 ID
         * @return drogon::Task<Result<admin::UserDetailResponse>> 用户详情响应
         */
        [[nodiscard]]
        auto GetUserDetail(uint64_t user_id)
            -> drogon::Task<Result<admin::UserDetailResponse>>;

        /**
         * @brief 修改用户状态
         *
         * 安全规则：
         * - target_id == operator_id 时返回 AdminCannotModifySelf
         * - 记录操作日志 admin.user.status_change
         *
         * @param target_id 目标用户 ID
         * @param status 新状态（0=禁用, 1=正常, 2=锁定）
         * @param operator_id 操作者 ID
         * @return drogon::Task<Result<void>>
         */
        [[nodiscard]]
        auto ChangeUserStatus(uint64_t target_id, int status, uint64_t operator_id)
            -> drogon::Task<Result<void>>;

        /**
         * @brief 修改用户角色
         *
         * 安全规则：
         * - target_id == operator_id 时返回 AdminCannotModifySelf
         * - 降级为普通用户时，检查是否为最后一个管理员
         * - 记录操作日志 admin.user.role_change
         *
         * @param target_id 目标用户 ID
         * @param role 新角色（0=普通用户, 1=管理员）
         * @param operator_id 操作者 ID
         * @return drogon::Task<Result<void>>
         */
        [[nodiscard]]
        auto ChangeUserRole(uint64_t target_id, int role, uint64_t operator_id)
            -> drogon::Task<Result<void>>;

        [[nodiscard]]
        auto ChangeUserAvailableSpace(uint64_t target_id,
                                      uint64_t available_space_g,
                                      uint64_t operator_id)
            -> drogon::Task<Result<admin::UserDetailResponse>>;

        /**
         * @brief 软删除用户
         *
         * 安全规则：
         * - target_id == operator_id 时返回 AdminCannotModifySelf
         * - 将用户状态设置为禁用（0）
         * - 记录操作日志 admin.user.soft_delete
         *
         * @param target_id 目标用户 ID
         * @param operator_id 操作者 ID
         * @return drogon::Task<Result<void>>
         */
        [[nodiscard]]
        auto SoftDeleteUser(uint64_t target_id, uint64_t operator_id)
            -> drogon::Task<Result<void>>;

        /**
         * @brief 获取全局存储统计
         *
         * @return drogon::Task<Result<admin::StorageStatsResponse>>
         */
        [[nodiscard]]
        auto GetGlobalStorageStats()
            -> drogon::Task<Result<admin::StorageStatsResponse>>;

        /**
         * @brief 获取分享列表
         *
         * 业务规则：
         * - 支持按状态、用户 ID 筛选
         * - 按 created_at DESC 排序
         * - 支持分页，自动计算 total_pages
         * - JOIN users 和 files 表获取用户名和文件名
         * - 记录操作日志 admin.share.list
         *
         * @param req 分享列表请求参数
         * @return drogon::Task<Result<admin::ShareListResponse>> 分享列表响应
         */
        [[nodiscard]]
        auto ListShares(const admin::ListSharesRequest& req)
            -> drogon::Task<Result<admin::ShareListResponse>>;

        /**
         * @brief 获取分享详情
         *
         * 业务规则：
         * - JOIN users 和 files 表获取用户名和文件名
         * - 不存在时返回 AdminShareNotFound
         * - 记录操作日志 admin.share.detail
         *
         * @param share_id 分享 ID
         * @return drogon::Task<Result<admin::ShareDetailResponse>> 分享详情响应
         */
        [[nodiscard]]
        auto GetShareDetail(uint64_t share_id)
            -> drogon::Task<Result<admin::ShareDetailResponse>>;

        /**
         * @brief 获取系统概览统计
         *
         * @details
         * 聚合查询用户数、文件数、存储用量、配额总量和活跃分享数。
         *
         * @return drogon::Task<Result<admin::StorageStatsResponse>>
         */
        [[nodiscard]]
        auto GetOverviewStats()
            -> drogon::Task<Result<admin::StorageStatsResponse>>;

        /**
         * @brief 获取系统状态
         *
         * @details
         * 检查 PostgreSQL/Redis 连接状态、磁盘空间和服务运行时间。
         *
         * @return drogon::Task<Result<admin::SystemStatusResponse>>
         */
        [[nodiscard]]
        auto GetSystemStatus()
            -> drogon::Task<Result<admin::SystemStatusResponse>>;

        /**
         * @brief 获取操作日志列表
         *
         * 业务规则：
         * - 支持按 action、start_date、end_date 筛选
         * - 按 created_at DESC 排序
         * - 支持分页，自动计算 total_pages
         * - 返回所有用户的操作日志（系统级）
         *
         * @param req 日志列表请求参数
         * @return drogon::Task<Result<admin::AdminLogListResponse>> 日志列表响应
         */
        [[nodiscard]]
        auto GetAdminLogs(const admin::AdminLogListRequest& req)
            -> drogon::Task<Result<admin::AdminLogListResponse>>;

        /**
         * @brief 强制取消分享
         *
         * 业务规则：
         * - 不检查分享所有权（管理员可取消任何分享）
         * - 将分享状态设置为已取消（status = 0）
         * - 不存在时返回 AdminShareNotFound
         * - 记录操作日志 admin.share.force_cancel
         *
         * @param share_id 分享 ID
         * @param operator_id 操作者 ID
         * @return drogon::Task<Result<void>>
         */
        [[nodiscard]]
        auto ForceCancelShare(uint64_t share_id, uint64_t operator_id)
            -> drogon::Task<Result<void>>;

    private:
        /**
         * @brief 私有构造函数（Singleton 模式）
         */
        AdminService();

        /**
         * @brief 记录操作日志
         *
         * @param operator_id 操作者 ID
         * @param action 操作动作
         * @param target_id 目标 ID
         * @param details 详细信息（JSON 字符串）
         * @return drogon::Task<void>
         */
        auto LogOperation(uint64_t operator_id,
                          const std::string& action,
                          const std::string& target_type,
                          uint64_t target_id,
                          const std::string& target_name,
                          const std::string& details) -> drogon::Task<void>;

        drogon::orm::DbClientPtr m_db_client; ///< 数据库客户端
        std::chrono::steady_clock::time_point m_start_time; ///< 服务启动时间
    };

} ///< namespace disk::services
