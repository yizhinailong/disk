/**
 * @file FileServiceUtils.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件服务共享工具函数和数据结构
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <drogon/orm/DbClient.h>
#include <json/writer.h>
#include <trantor/utils/Date.h>

#include "models/Files.hpp"
#include "models/Folders.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/LogHelper.hpp"

namespace disk::file::utils {

    using drogon_model::disk::Files;
    using drogon_model::disk::Folders;

    template <typename BindParameters>
    auto ExecSqlWithBindings(
        const drogon::orm::DbClientPtr& client,
        const std::string& sql,
        BindParameters bind_parameters
    ) -> drogon::Task<drogon::orm::Result> {
        auto binder = *client << sql;
        bind_parameters(binder);
        co_return co_await drogon::orm::internal::SqlAwaiter(std::move(binder));
    }

    struct TrashInsertItem {
        std::string item_type{ "file" };
        uint64_t item_id;
        std::string item_name;
        uint64_t item_size;
        uint64_t original_folder_id;
        std::string original_path;
        std::optional<uint64_t> content_id;
        std::string item_data;
    };

    struct FolderDeletePlan {
        Folders root;
        std::vector<Folders> folders;
        std::vector<Files> files;
        uint64_t item_size{ 0 };
    };

    struct FolderLocation {
        std::string path{ "/" };
        uint32_t depth{ 0 };
    };

    [[nodiscard]] auto ExtractFileExtension(const std::string& filename) -> std::string;

    [[nodiscard]] auto BuildFilePath(const std::string& folder_path, const std::string& filename)
        -> std::string;

    [[nodiscard]] auto BuildFolderPath(const std::string& parent_path, const std::string& name)
        -> std::string;

    auto QueryOccupiedFolderNames(
        const drogon::orm::DbClientPtr& client,
        uint64_t parent_id,
        uint64_t user_id,
        const std::vector<std::string>& candidate_names
    ) -> drogon::Task<std::unordered_set<std::string>>;

    auto QueryOccupiedNames(
        const drogon::orm::DbClientPtr& client,
        uint64_t folder_id,
        uint64_t user_id,
        const std::vector<std::string>& candidate_names
    ) -> drogon::Task<std::unordered_set<std::string>>;

    auto InsertTrashRecords(
        const drogon::orm::DbClientPtr& client,
        const std::vector<TrashInsertItem>& trash_items,
        uint64_t user_id,
        disk::utils::LogContext log_context = {}
    ) -> drogon::Task<bool>;

    [[nodiscard]] auto DateToJson(const trantor::Date& date) -> std::string;

    [[nodiscard]] auto BuildFolderSnapshot(const FolderDeletePlan& plan) -> std::string;

    [[nodiscard]] auto FilterCoveredFolderIds(
        const std::vector<uint64_t>& requested_folder_ids,
        const std::unordered_map<uint64_t, FolderDeletePlan>& plans
    ) -> std::vector<uint64_t>;

    [[nodiscard]] auto CollectCoveredFileIds(
        const std::vector<uint64_t>& top_level_folder_ids,
        const std::unordered_map<uint64_t, FolderDeletePlan>& plans
    ) -> std::unordered_set<uint64_t>;

    auto DeleteFilesByIds(
        const drogon::orm::DbClientPtr& client,
        const std::vector<uint64_t>& file_ids,
        disk::utils::LogContext log_context = {}
    ) -> drogon::Task<int>;

    auto DeleteFoldersByIds(
        const drogon::orm::DbClientPtr& client,
        const std::vector<uint64_t>& folder_ids,
        disk::utils::LogContext log_context = {}
    ) -> drogon::Task<int>;

    [[nodiscard]] auto NormalizeFulltextKeyword(std::string_view keyword) -> std::string;

    [[nodiscard]] auto IsFulltextEligible(std::string_view keyword) -> bool;

    [[nodiscard]] auto ResolveListSortColumn(
        std::string_view sort_by,
        bool folder_only
    ) -> std::string_view;

    [[nodiscard]] auto BuildDeterministicOrderByClause(
        std::string_view primary_column,
        std::string_view direction,
        bool include_type,
        std::string_view prefix = {}
    ) -> std::string;

} // namespace disk::file::utils
