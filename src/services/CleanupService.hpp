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

namespace disk::services {

    namespace cleanup_internal {
        [[nodiscard]] auto BuildNumericInClause(const std::vector<uint64_t>& ids) -> std::string;
    }

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
    class CleanupService {
    public:
        explicit CleanupService(drogon::orm::DbClientPtr db_client);
        ~CleanupService() = default;
        CleanupService(const CleanupService&) = delete;
        auto operator=(const CleanupService&) -> CleanupService& = delete;
        CleanupService(CleanupService&&) = default;
        auto operator=(CleanupService&&) -> CleanupService& = default;

        /**
         * @brief 清理过期的回收站项目
         *
         * 业务规则：
         * - 查找所有 expires_at < NOW() 的回收站记录
         * - 逐个处理，释放存储配额并更新引用计数
         * - 删除回收站记录
         *
         * @return drogon::Task<Result<int>> 成功返回清理数量，失败返回错误
         */
        [[nodiscard]]
        auto CleanupExpiredTrash() -> drogon::Task<Result<int>>;

        /**
         * @brief 清理过期的上传任务
         *
         * 业务规则：
         * - 查找 status = 0 (进行中) 且 expires_at < NOW() 的上传任务
         * - 将状态更新为 3 (已过期)
         * - 清理临时文件
         * - 每批次处理最多 100 条记录（bounded）
         * - 幂等操作：重复执行不会产生副作用
         *
         * @return drogon::Task<Result<int>> 成功返回清理数量，失败返回错误
         */
        [[nodiscard]]
        auto CleanupExpiredUploadTasks() -> drogon::Task<Result<int>>;

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

        /**
         * @brief 更新文件内容引用计数
         *
         * @param content_id 文件内容 ID
         * @return drogon::Task<void>
         */
        auto DecrementContentRefCount(uint64_t content_id) -> drogon::Task<void>;
    };

} // namespace disk::services
