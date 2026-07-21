/**
 * @file CleanupService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 系统清理服务
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "utils/ErrorCode.hpp"
#include "utils/LogHelper.hpp"

namespace disk::services {

    /**
     * @brief 系统清理服务类
     *
     * 提供系统定时清理功能：
     * - 清理过期的回收站项目（expires_at < NOW()）
     * - 释放用户存储配额
     * - 更新文件内容引用计数
     *
     * 业务规则：
     * - 清理 expires_at < NOW() 的回收站记录
     * - 每个项目清理后释放对应存储空间
     * - 更新 file_contents.ref_count
     */
    struct CleanupRunResult {
        int expired_trash_deleted{ 0 };
        int expired_upload_tasks_cleaned{ 0 };
    };

    class CleanupService {
    public:
        explicit CleanupService(drogon::orm::DbClientPtr db_client);
        ~CleanupService() = default;
        CleanupService(const CleanupService&) = delete;
        auto operator=(const CleanupService&) -> CleanupService& = delete;
        CleanupService(CleanupService&&) = default;
        auto operator=(CleanupService&&) -> CleanupService& = default;

        /**
         * @brief 确定性运行一次过期数据清理
         *
         * 与生产定时任务使用同一清理实现，便于测试和手动维护稳定触发。
         *
         * @return drogon::Task<Result<CleanupRunResult>> 成功返回本次清理结果
         */
        [[nodiscard]]
        auto RunExpiredCleanupOnce(disk::utils::LogContext log_context = {})
            -> drogon::Task<Result<CleanupRunResult>>;

        /**
         * @brief 清理过期的回收站项目
         *
         * 业务规则：
         * - 使用游标分批查找 expires_at < NOW() 的回收站记录（id ASC + LIMIT）
         * - 每批内部分块事务处理，释放存储配额并更新引用计数
         * - 单页任一分块失败时不推进持久游标，由 Worker 重试
         * - 单次运行最多处理 kMaxTrashBatchesPerRun 批次（保守有界）
         * - blob 删除前在事务外二次验证 ref_count=0（防止并发上传竞争）
         * - 删除回收站记录
         *
         * @return drogon::Task<Result<int>> 成功返回清理数量，失败返回错误
         */
        [[nodiscard]]
        auto CleanupExpiredTrash(disk::utils::LogContext log_context = {})
            -> drogon::Task<Result<int>>;

        /**
         * @brief 清理过期的上传任务
         *
         * 业务规则：
         * - 查找 status = 0 (进行中) 且 expires_at < NOW() 的上传任务
         * - 将状态更新为 3 (已过期)
         * - 清理临时文件
         * - 单次运行最多处理 kUploadTaskCleanupBatchSize 条记录（单趟有界）
         * - 幂等操作：重复执行不会产生副作用
         *
         * @return drogon::Task<Result<int>> 成功返回清理数量，失败返回错误
         */
        [[nodiscard]]
        auto CleanupExpiredUploadTasks(disk::utils::LogContext log_context = {})
            -> drogon::Task<Result<int>>;

    private:
        drogon::orm::DbClientPtr m_db_client;

        /**
         * @brief 更新用户存储使用量
         *
         * @param user_id 用户 ID
         * @param delta 变化量（负数为减少）
         * @return drogon::Task<void>
         */
        auto UpdateStorageUsed(uint64_t user_id, int64_t delta) -> drogon::Task<void>;
    };

} // namespace disk::services
