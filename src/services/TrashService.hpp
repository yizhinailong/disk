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
 * - V1 不支持递归恢复文件夹子树
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "dtos/TrashDto.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::trash {

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
         * @return drogon::Task<Result<std::vector<TrashItemResponse>>> 成功返回项目列表，失败返回错误
         */
        [[nodiscard]]
        auto List(uint64_t user_id, int page, int page_size)
            -> drogon::Task<Result<std::vector<TrashItemResponse>>>;

        /**
         * @brief 统计用户回收站项目总数
         *
         * @param user_id 用户 ID
         * @return drogon::Task<Result<int>> 成功返回总数，失败返回错误
         */
        [[nodiscard]]
        auto Count(uint64_t user_id) -> drogon::Task<Result<int>>;

        /**
         * @brief 批量恢复回收站项目
         *
         * 业务规则：
         * - 验证每个回收站项目的归属
         * - 检查原始文件夹是否存在，不存在则恢复到根目录
         * - 检测目标位置文件名冲突，自动重命名为 name (n).ext
         * - V1 不支持递归恢复文件夹子树，文件夹恢复仅恢复空文件夹
         * - 返回每项操作的详细结果（成功/失败及原因）
         *
         * @param user_id 用户 ID
         * @param trash_ids 回收站项目 ID 列表
         * @return drogon::Task<Result<BatchRestoreResponse>> 成功返回批量恢复结果，失败返回错误
         */
        [[nodiscard]]
        auto Restore(uint64_t user_id, const std::vector<uint64_t>& trash_ids)
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
         * @return drogon::Task<Result<BatchDeleteResponse>> 成功返回批量删除结果，失败返回错误
         */
        [[nodiscard]]
        auto Delete(uint64_t user_id, const std::vector<uint64_t>& trash_ids)
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
         * @return drogon::Task<Result<DeleteAllResponse>> 成功返回清空结果，失败返回错误
         */
        [[nodiscard]]
        auto DeleteAll(uint64_t user_id) -> drogon::Task<Result<DeleteAllResponse>>;

    private:
        struct PrefetchedTrashItem {
            uint64_t id{ 0 };
            uint64_t user_id{ 0 };
            std::string item_type;
            uint64_t item_id{ 0 };
            std::string item_name;
            uint64_t item_size{ 0 };
            uint64_t original_folder_id{ 0 };
            std::string original_path;
            std::string item_data;
            std::optional<uint64_t> content_id;
        };

        /**
         * @brief 生成唯一文件名（用于冲突自动重命名）
         *
         * @details
         * 当目标文件夹存在同名文件时，生成新的唯一文件名：
         * - 文件：name (n).ext（n 从 1 开始递增）
         * - 文件夹：name (n)（n 从 1 开始递增）
         *
         * @param folder_id 目标文件夹 ID
         * @param name 原始文件名
         * @param user_id 用户 ID
         * @param is_file 是否为文件（true: 文件，false: 文件夹）
         * @return drogon::Task<std::string> 唯一文件名
         */
        [[nodiscard]]
        auto GenerateUniqueFilename(
            uint64_t folder_id,
            const std::string& name,
            uint64_t user_id,
            bool is_file
        ) -> drogon::Task<std::string>;

        /**
         * @brief 检查文件名是否已存在
         *
         * @param folder_id 文件夹 ID
         * @param filename 文件名
         * @param user_id 用户 ID
         * @return drogon::Task<bool> 存在返回 true
         */
        [[nodiscard]]
        auto IsFilenameExists(uint64_t folder_id, const std::string& filename, uint64_t user_id) const
            -> drogon::Task<bool>;

        /**
         * @brief 检查文件夹名是否已存在
         *
         * @param folder_id 父文件夹 ID
         * @param foldername 文件夹名
         * @param user_id 用户 ID
         * @return drogon::Task<bool> 存在返回 true
         */
        [[nodiscard]]
        auto IsFolderNameExists(uint64_t folder_id, const std::string& foldername, uint64_t user_id) const
            -> drogon::Task<bool>;

        /**
         * @brief 检查文件夹是否存在且属于用户
         *
         * @param folder_id 文件夹 ID
         * @param user_id 用户 ID
         * @return drogon::Task<bool> 存在且属于用户返回 true
         */
        [[nodiscard]]
        auto IsFolderExists(uint64_t folder_id, uint64_t user_id) const -> drogon::Task<bool>;

        /**
         * @brief 更新用户存储使用量
         *
         * @param user_id 用户 ID
         * @param delta 变化量（正数为增加，负数为减少）
         * @return drogon::Task<void>
         */
        auto UpdateStorageUsed(uint64_t user_id, int64_t delta) -> drogon::Task<void>;

        /**
         * @brief 恢复单个文件
         *
         * @param trash_id 回收站项目 ID
         * @param user_id 用户 ID
         * @param result 输出参数：操作结果
         * @return drogon::Task<void>
         */
        auto RestoreFile(uint64_t trash_id, uint64_t user_id, BatchResultItem& result) -> drogon::Task<void>;
        auto RestoreFile(const PrefetchedTrashItem& trash_item, uint64_t user_id, BatchResultItem& result)
            -> drogon::Task<void>;

        /**
         * @brief 恢复单个文件夹
         *
         * @param trash_id 回收站项目 ID
         * @param user_id 用户 ID
         * @param result 输出参数：操作结果
         * @return drogon::Task<void>
         */
        auto RestoreFolder(uint64_t trash_id, uint64_t user_id, BatchResultItem& result) -> drogon::Task<void>;
        auto RestoreFolder(
            const PrefetchedTrashItem& trash_item,
            uint64_t user_id,
            BatchResultItem& result
        ) -> drogon::Task<void>;

        /**
         * @brief 永久删除单个文件
         *
         * @param trash_id 回收站项目 ID
         * @param user_id 用户 ID
         * @param result 输出参数：操作结果
         * @return drogon::Task<uint64_t> 返回释放的空间大小
         */
        auto DeleteFile(uint64_t trash_id, uint64_t user_id, BatchResultItem& result) -> drogon::Task<uint64_t>;
        auto DeleteFile(const PrefetchedTrashItem& trash_item, uint64_t user_id, BatchResultItem& result)
            -> drogon::Task<uint64_t>;

        /**
         * @brief 永久删除单个文件夹
         *
         * @param trash_id 回收站项目 ID
         * @param user_id 用户 ID
         * @param result 输出参数：操作结果
         * @return drogon::Task<uint64_t> 返回释放的空间大小
         */
        auto DeleteFolder(uint64_t trash_id, uint64_t user_id, BatchResultItem& result) -> drogon::Task<uint64_t>;
        auto DeleteFolder(const PrefetchedTrashItem& trash_item, uint64_t user_id, BatchResultItem& result)
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

        drogon::orm::DbClientPtr m_db_client; ///< 数据库客户端
    };

} ///< namespace disk::trash
