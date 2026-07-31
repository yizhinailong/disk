/**
 * @file TrashService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 回收站服务
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 提供回收站相关功能：
 * - 列出回收站项目（分页）
 * - 批量恢复文件/文件夹（支持冲突自动重命名）
 * - 批量彻底删除（释放存储配额）
 * - 清空回收站（释放存储配额）
 *
 * 业务规则：
 * - 回收站内容计入 storage_used
 * - 仅在彻底删除时释放配额
 * - 恢复时如目标位置存在同名文件，自动重命名为 name (n).ext
 * - 原始文件夹不存在时恢复到根目录
 * - 带 folder_tree 快照的文件夹项递归恢复完整子树
 */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "dtos/TrashDto.hpp"
#include "services/FileServiceUtils.hpp"
#include "services/RedisService.hpp"
#include "services/TrashQuery.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/LogHelper.hpp"

namespace disk::trash {

    struct MoveToTrashRequest {
        std::vector<uint64_t> file_ids;
        std::vector<uint64_t> folder_ids;
    };

    struct MoveToTrashResult {
        int deleted_count{ 0 };
        int deleted_file_count{ 0 };
        int deleted_folder_count{ 0 };
        std::vector<uint64_t> removed_file_ids;
        std::vector<uint64_t> removed_folder_ids;
    };

    struct ExpiredTrashCleanupPageResult {
        size_t candidates{ 0 };
        size_t deleted{ 0 };
        uint64_t next_after_id{ 0 };
        bool has_more{ false };
    };

    /**
     * @brief 回收站服务类
     *
     * @details
     * 提供回收站的业务逻辑处理：
     * - List: 查询用户的回收站项目列表（分页）
     * - Restore: 批量恢复文件/文件夹，支持冲突自动重命名
     * - Delete: 批量彻底删除，释放存储配额
     * - DeleteAll: 清空回收站，释放存储配额
     *
     * 恢复规则：
     * - 验证回收站项目归属
     * - 检查原始文件夹是否存在，不存在则恢复到根目录
     * - 检测目标位置文件名冲突，自动重命名为 name (n).ext
     * - 返回每项操作的详细结果
     *
     * 删除规则：
     * - 永久删除回收站记录
     * - 释放用户存储配额
     * - 更新文件内容引用计数（ref_count）
     * - 返回每项操作的详细结果
     */
    class TrashService {
    public:
        /**
         * @brief 构造函数
         * @param db_client 数据库客户端
         */
        explicit TrashService(drogon::orm::DbClientPtr db_client);
        ~TrashService() = default;
        TrashService(const TrashService&) = delete;
        auto operator=(const TrashService&) -> TrashService& = delete;
        TrashService(TrashService&&) = default;
        auto operator=(TrashService&&) -> TrashService& = default;

        [[nodiscard]]
        auto CreateTrashRecords(
            const drogon::orm::DbClientPtr& client,
            const std::vector<disk::file::utils::TrashInsertItem>& trash_items,
            uint64_t user_id,
            disk::utils::LogContext log_context = {}
        ) const -> drogon::Task<bool>;

        /**
         * @brief 将文件和文件夹移入回收站
         * @param request 待删除项目
         * @param user_id 用户 ID
         * @param log_context 请求日志上下文
         */
        [[nodiscard]]
        auto MoveToTrash(
            MoveToTrashRequest request,
            uint64_t user_id,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<MoveToTrashResult>>;

        [[nodiscard]]
        auto CleanupExpiredTrashItems(
            int fetch_batch_size,
            int max_batches_per_run,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<int>>;

        [[nodiscard]]
        auto CleanupExpiredTrashPage(
            uint64_t after_id,
            size_t limit,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<ExpiredTrashCleanupPageResult>>;

        /**
         * @brief 列出回收站项目
         *
         * 业务规则：
         * - 按 deleted_at DESC 排序
         * - 返回分页结果
         * - 包含项目类型、大小、原始路径等信息
         *
         * @param user_id 用户 ID
         * @param page 页码（从 1 开始）
         * @param page_size 每页数量
         * @param log_context 请求日志上下文
         * @return drogon::Task<Result<std::vector<TrashItemResponse>>> 成功返回项目列表，失败返回错误
         */
        [[nodiscard]]
        auto List(
            uint64_t user_id,
            int page,
            int page_size,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<std::vector<TrashItemResponse>>>;

        /**
         * @brief 统计用户回收站项目总数
         *
         * @param user_id 用户 ID
         * @param log_context 请求日志上下文
         * @return drogon::Task<Result<int>> 成功返回总数，失败返回错误
         */
        [[nodiscard]]
        auto Count(uint64_t user_id, disk::utils::LogContext log_context = {})
            -> drogon::Task<Result<int>>;

        /**
         * @brief 批量恢复回收站项目
         *
         * 业务规则：
         * - 验证每个回收站项目的归属
         * - 检查原始文件夹是否存在，不存在则恢复到根目录
         * - 检测目标位置文件名冲突，自动重命名为 name (n).ext
         * - 带 folder_tree 快照的文件夹项递归恢复完整子树，历史兼容项恢复空根
         * - 返回每项操作的详细结果（成功/失败及原因）
         *
         * @param user_id 用户 ID
         * @param trash_ids 回收站项目 ID 列表
         * @param log_context 请求日志上下文
         * @return drogon::Task<Result<BatchRestoreResponse>> 成功返回批量恢复结果，失败返回错误
         */
        [[nodiscard]]
        auto Restore(
            uint64_t user_id,
            const std::vector<uint64_t>& trash_ids,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<BatchRestoreResponse>>;

        /**
         * @brief 批量彻底删除回收站项目
         *
         * 业务规则：
         * - 验证每个回收站项目的归属
         * - 永久删除回收站记录
         * - 释放用户存储配额
         * - 更新文件内容引用计数（ref_count）
         * - 返回每项操作的详细结果（成功/失败及原因）
         *
         * @param user_id 用户 ID
         * @param trash_ids 回收站项目 ID 列表
         * @param log_context 请求日志上下文
         * @return drogon::Task<Result<BatchDeleteResponse>> 成功返回批量删除结果，失败返回错误
         */
        [[nodiscard]]
        auto Delete(
            uint64_t user_id,
            const std::vector<uint64_t>& trash_ids,
            disk::utils::LogContext log_context = {}
        )
            -> drogon::Task<Result<BatchDeleteResponse>>;

        /**
         * @brief 清空回收站
         *
         * 业务规则：
         * - 删除用户所有回收站项目
         * - 释放总存储配额
         * - 更新文件内容引用计数
         * - 返回删除数量和释放空间
         *
         * @param user_id 用户 ID
         * @param log_context 请求日志上下文
         * @return drogon::Task<Result<DeleteAllResponse>> 成功返回清空结果，失败返回错误
         */
        [[nodiscard]]
        auto DeleteAll(uint64_t user_id, disk::utils::LogContext log_context = {})
            -> drogon::Task<Result<DeleteAllResponse>>;

    private:
        struct PermanentDeleteResult {
            int deleted_count{ 0 };
            uint64_t freed_space{ 0 };
        };

        struct ShareCleanupStats {
            int deleted_file_share_links{ 0 };
            int deleted_folder_share_links{ 0 };
            int cancelled_empty_shares{ 0 };
        };

        [[nodiscard]]
        auto CleanupShareLinksForMovedItems(
            const drogon::orm::DbClientPtr& client,
            const std::vector<uint64_t>& file_ids,
            const std::vector<uint64_t>& folder_ids
        ) const -> drogon::Task<ShareCleanupStats>;

        [[nodiscard]]
        auto PermanentlyDeleteTrashItems(
            const std::vector<TrashLifecycleRecord>& trash_items,
            bool require_valid_file_content,
            disk::utils::LogContext log_context = {}
        ) -> drogon::Task<PermanentDeleteResult>;

        /**
         * @brief 恢复单个文件
         *
         * @param trash_item 回收站生命周期记录
         * @param user_id 用户 ID
         * @param result 输出参数：操作结果
         * @param log_context 请求日志上下文
         * @return drogon::Task<void>
         */
        auto RestoreFile(
            const TrashLifecycleRecord& trash_item,
            uint64_t user_id,
            BatchResultItem& result,
            disk::utils::LogContext log_context
        )
            -> drogon::Task<void>;

        /**
         * @brief 恢复单个文件夹
         *
         * @param trash_item 回收站生命周期记录
         * @param user_id 用户 ID
         * @param result 输出参数：操作结果
         * @param log_context 请求日志上下文
         * @return drogon::Task<void>
         */
        auto RestoreFolder(
            const TrashLifecycleRecord& trash_item,
            uint64_t user_id,
            BatchResultItem& result,
            disk::utils::LogContext log_context
        ) -> drogon::Task<void>;

        /**
         * @brief 永久删除单个文件
         *
         * @param trash_item 回收站生命周期记录
         * @param user_id 用户 ID
         * @param result 输出参数：操作结果
         * @param log_context 请求日志上下文
         * @return drogon::Task<uint64_t> 返回释放的空间大小
         */
        auto DeleteFile(
            const TrashLifecycleRecord& trash_item,
            uint64_t user_id,
            BatchResultItem& result,
            disk::utils::LogContext log_context
        )
            -> drogon::Task<uint64_t>;

        /**
         * @brief 永久删除单个文件夹
         *
         * @param trash_item 回收站生命周期记录
         * @param user_id 用户 ID
         * @param result 输出参数：操作结果
         * @param log_context 请求日志上下文
         * @return drogon::Task<uint64_t> 返回释放的空间大小
         */
        auto DeleteFolder(
            const TrashLifecycleRecord& trash_item,
            uint64_t user_id,
            BatchResultItem& result,
            disk::utils::LogContext log_context
        )
            -> drogon::Task<uint64_t>;

        /**
         * @brief 从文件名提取扩展名
         *
         * @param filename 文件名
         * @return std::string 扩展名（不含点）
         */
        [[nodiscard]]
        static auto ExtractExtension(const std::string& filename) -> std::string;

        /**
         * @brief 从文件名提取基础名称（不含扩展名）
         *
         * @param filename 文件名
         * @return std::string 基础名称
         */
        [[nodiscard]]
        static auto ExtractBaseName(const std::string& filename) -> std::string;

        drogon::orm::DbClientPtr m_db_client;                                                                         ///< 数据库客户端
        TrashQuery m_trash_query;                                                                                     ///< 回收站查询边界
        std::shared_ptr<disk::services::RedisService> m_redis_service{ disk::services::RedisService::GetInstance() }; ///< Redis 服务
    };

} // namespace disk::trash
