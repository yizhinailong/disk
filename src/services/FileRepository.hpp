/**
 * @file FileRepository.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件持久化原语
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>
#include <trantor/utils/Date.h>

#include "models/Files.hpp"

namespace disk::file {

    /**
     * @brief 文件持久化原语
     *
     * @details
     * 仅封装文件归属查询、批量读取和路径/位置更新等数据访问操作。
     * 不承载业务决策、事务创建或缓存失效。
     */
    class FileRepository {
    public:
        [[nodiscard]]
        auto FindOwnedFileForUpdate(
            const drogon::orm::DbClientPtr& client,
            uint64_t file_id,
            uint64_t user_id
        ) const -> drogon::Task<std::optional<drogon_model::disk::Files>>;

        auto AcquireNameLock(
            const drogon::orm::DbClientPtr& client,
            uint64_t user_id,
            uint64_t folder_id,
            const std::string& name
        ) const -> drogon::Task<void>;

        [[nodiscard]]
        auto NameExistsExcluding(
            const drogon::orm::DbClientPtr& client,
            const std::string& name,
            uint64_t folder_id,
            uint64_t user_id,
            uint64_t excluded_file_id
        ) const -> drogon::Task<bool>;

        [[nodiscard]]
        auto FetchOwnedFilesByIdsForUpdate(
            const drogon::orm::DbClientPtr& client,
            const std::vector<uint64_t>& file_ids,
            uint64_t user_id
        ) const -> drogon::Task<std::vector<drogon_model::disk::Files>>;

        [[nodiscard]]
        auto FetchFilesInFolders(
            const drogon::orm::DbClientPtr& client,
            const std::vector<uint64_t>& folder_ids,
            uint64_t user_id
        ) const -> drogon::Task<std::vector<drogon_model::disk::Files>>;

        [[nodiscard]]
        auto RenameOwnedFile(
            const drogon::orm::DbClientPtr& client,
            uint64_t file_id,
            uint64_t user_id,
            const std::string& new_name,
            const std::string& extension,
            const std::string& new_path,
            const trantor::Date& updated_at
        ) const -> drogon::Task<bool>;

        [[nodiscard]]
        auto UpdateFileLocation(
            const drogon::orm::DbClientPtr& client,
            uint64_t file_id,
            uint64_t user_id,
            uint64_t folder_id,
            const std::string& new_path,
            const trantor::Date& updated_at
        ) const -> drogon::Task<bool>;

        [[nodiscard]]
        auto UpdateFilePath(
            const drogon::orm::DbClientPtr& client,
            uint64_t file_id,
            uint64_t user_id,
            const std::string& new_path,
            const trantor::Date& updated_at
        ) const -> drogon::Task<bool>;
    };

} ///< namespace disk::file
