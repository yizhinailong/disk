/**
 * @file FileServiceUtils.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件服务共享工具函数实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "FileServiceUtils.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "utils/BatchUtils.hpp"

namespace disk::file::utils {

    using disk::utils::BatchUtils;

    [[nodiscard]] auto ExtractFileExtension(const std::string& filename) -> std::string {
        const auto pos = filename.rfind('.');
        if (pos == std::string::npos || pos == filename.length() - 1) {
            return "";
        }
        return filename.substr(pos + 1);
    }

    [[nodiscard]] auto BuildFilePath(const std::string& folder_path, const std::string& filename)
        -> std::string {
        return folder_path == "/" ? "/" + filename : folder_path + filename;
    }

    [[nodiscard]] auto BuildFolderPath(const std::string& parent_path, const std::string& name)
        -> std::string {
        return parent_path == "/" ? "/" + name + "/" : parent_path + name + "/";
    }

    auto QueryOccupiedFolderNames(
        const drogon::orm::DbClientPtr& client,
        uint64_t parent_id,
        uint64_t user_id,
        const std::vector<std::string>& candidate_names
    ) -> drogon::Task<std::unordered_set<std::string>> {
        std::unordered_set<std::string> occupied_names;
        if (candidate_names.empty()) {
            co_return occupied_names;
        }

            auto sql =
                "SELECT name FROM folders WHERE parent_id = $1 AND user_id = $2 AND name IN (" +
                BatchUtils::BuildInPlaceholders(candidate_names, 3) + ")";
        auto result = co_await ExecSqlWithBindings(
            client,
            sql,
            [&](auto& binder) {
                binder << parent_id << user_id;
                for (const auto& candidate_name : candidate_names) {
                    binder << candidate_name;
                }
            }
        );

        occupied_names.reserve(result.size());
        for (const auto& row : result) {
            occupied_names.insert(row["name"].as<std::string>());
        }
        co_return occupied_names;
    }

    auto QueryOccupiedNames(
        const drogon::orm::DbClientPtr& client,
        uint64_t folder_id,
        uint64_t user_id,
        const std::vector<std::string>& candidate_names
    ) -> drogon::Task<std::unordered_set<std::string>> {
        std::unordered_set<std::string> occupied_names;
        if (candidate_names.empty()) {
            co_return occupied_names;
        }

        auto sql =
            "SELECT name FROM files WHERE folder_id = $1 AND user_id = $2 AND name IN (" +
            BatchUtils::BuildInPlaceholders(candidate_names, 3) + ")";
        auto result = co_await ExecSqlWithBindings(
            client,
            sql,
            [&](auto& binder) {
                binder << folder_id << user_id;
                for (const auto& candidate_name : candidate_names) {
                    binder << candidate_name;
                }
            }
        );

        occupied_names.reserve(result.size());
        for (const auto& row : result) {
            occupied_names.insert(row["name"].as<std::string>());
        }

        co_return occupied_names;
    }

    auto InsertTrashRecords(
        const drogon::orm::DbClientPtr& client,
        const std::vector<TrashInsertItem>& trash_items,
        uint64_t user_id,
        disk::utils::LogContext log_context
    ) -> drogon::Task<bool> {
        if (trash_items.empty()) {
            co_return true;
        }

        std::string insert_sql =
            "INSERT INTO trash (user_id, item_type, item_id, item_name, item_size, " "content_id, original_folder_id, original_path, item_data, " "deleted_at, expires_at) VALUES ";

        int base = 1;
        const int cols_per_row = 9;
        for (size_t i = 0; i < trash_items.size(); ++i) {
            if (i > 0) {
                insert_sql += ",";
            }
            int p = base + static_cast<int>(i) * cols_per_row;
            insert_sql += "($" + std::to_string(p) + ", $" +
                          std::to_string(p + 1) + ", $" +
                          std::to_string(p + 2) + ", $" +
                          std::to_string(p + 3) + ", $" +
                          std::to_string(p + 4) + ", $" +
                          std::to_string(p + 5) + ", $" +
                          std::to_string(p + 6) + ", $" +
                          std::to_string(p + 7) + ", $" +
                          std::to_string(p + 8) +
                          ", NOW(), NOW() + INTERVAL '30 days')";
        }

        try {
            co_await ExecSqlWithBindings(
                client,
                insert_sql,
                [&](auto& binder) {
                    for (const auto& item : trash_items) {
                        binder << user_id << item.item_type << item.item_id << item.item_name
                               << item.item_size << item.content_id << item.original_folder_id
                               << item.original_path << item.item_data;
                    }
                }
            );
            co_return true;
        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Warn(log_context) << "Trash record batch insert failed";
            co_return false;
        }
    }

    [[nodiscard]] auto DateToJson(const trantor::Date& date) -> std::string {
        return date.toDbStringLocal();
    }

    [[nodiscard]] auto BuildFolderSnapshot(const FolderDeletePlan& plan) -> std::string {
        Json::Value item_data;
        item_data["type"] = "folder_tree";
        item_data["version"] = 1;

        Json::Value root;
        root["id"] = static_cast<Json::UInt64>(plan.root.getValueOfId());
        root["parent_id"] = static_cast<Json::UInt64>(plan.root.getValueOfParentId());
        root["name"] = plan.root.getValueOfName();
        root["path"] = plan.root.getValueOfPath();
        root["depth"] = plan.root.getValueOfDepth();
        root["item_count"] = plan.root.getValueOfItemCount();
        root["created_at"] = DateToJson(plan.root.getValueOfCreatedAt());
        root["updated_at"] = DateToJson(plan.root.getValueOfUpdatedAt());
        item_data["root"] = root;

        Json::Value folders(Json::arrayValue);
        for (const auto& folder : plan.folders) {
            if (folder.getValueOfId() == plan.root.getValueOfId()) {
                continue;
            }

            Json::Value value;
            value["id"] = static_cast<Json::UInt64>(folder.getValueOfId());
            value["parent_id"] = static_cast<Json::UInt64>(folder.getValueOfParentId());
            value["name"] = folder.getValueOfName();
            value["path"] = folder.getValueOfPath();
            value["depth"] = folder.getValueOfDepth();
            value["item_count"] = folder.getValueOfItemCount();
            value["created_at"] = DateToJson(folder.getValueOfCreatedAt());
            value["updated_at"] = DateToJson(folder.getValueOfUpdatedAt());
            folders.append(value);
        }
        item_data["folders"] = folders;

        Json::Value files(Json::arrayValue);
        for (const auto& file : plan.files) {
            Json::Value value;
            value["id"] = static_cast<Json::UInt64>(file.getValueOfId());
            value["folder_id"] = static_cast<Json::UInt64>(file.getValueOfFolderId());
            if (file.getContentId()) {
                value["content_id"] = static_cast<Json::UInt64>(*file.getContentId());
            }
            value["name"] = file.getValueOfName();
            value["extension"] = file.getValueOfExtension();
            value["size"] = static_cast<Json::UInt64>(file.getValueOfSize());
            value["mime_type"] = file.getValueOfMimeType();
            value["path"] = file.getValueOfPath();
            value["is_favorite"] = file.getValueOfIsFavorite();
            value["download_count"] = file.getValueOfDownloadCount();
            value["created_at"] = DateToJson(file.getValueOfCreatedAt());
            value["updated_at"] = DateToJson(file.getValueOfUpdatedAt());
            files.append(value);
        }
        item_data["files"] = files;

        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        return Json::writeString(builder, item_data);
    }

    [[nodiscard]] auto FilterCoveredFolderIds(
        const std::vector<uint64_t>& requested_folder_ids,
        const std::unordered_map<uint64_t, FolderDeletePlan>& plans
    ) -> std::vector<uint64_t> {
        std::vector<uint64_t> result;
        result.reserve(requested_folder_ids.size());

        for (const auto folder_id : requested_folder_ids) {
            auto plan_it = plans.find(folder_id);
            if (plan_it == plans.end()) {
                continue;
            }

            bool covered_by_ancestor = false;
            for (const auto& [candidate_id, candidate_plan] : plans) {
                if (candidate_id == folder_id) {
                    continue;
                }
                auto contains = std::any_of(
                    candidate_plan.folders.begin(),
                    candidate_plan.folders.end(),
                    [folder_id](const Folders& folder) {
                        return folder.getValueOfId() == folder_id;
                    }
                );
                if (contains) {
                    covered_by_ancestor = true;
                    break;
                }
            }

            if (!covered_by_ancestor) {
                result.push_back(folder_id);
            }
        }

        return result;
    }

    [[nodiscard]] auto CollectCoveredFileIds(
        const std::vector<uint64_t>& top_level_folder_ids,
        const std::unordered_map<uint64_t, FolderDeletePlan>& plans
    ) -> std::unordered_set<uint64_t> {
        std::unordered_set<uint64_t> file_ids;
        for (const auto folder_id : top_level_folder_ids) {
            auto plan_it = plans.find(folder_id);
            if (plan_it == plans.end()) {
                continue;
            }
            for (const auto& file : plan_it->second.files) {
                file_ids.insert(file.getValueOfId());
            }
        }
        return file_ids;
    }

    auto DeleteFilesByIds(
        const drogon::orm::DbClientPtr& client,
        const std::vector<uint64_t>& file_ids,
        disk::utils::LogContext log_context
    ) -> drogon::Task<int> {
        if (file_ids.empty()) {
            co_return 0;
        }

        try {
            auto result = co_await client->execSqlCoro(
                "DELETE FROM files WHERE id IN (" +
                BatchUtils::BuildSafeNumericInClause(file_ids) + ")"
            );
            co_return static_cast<int>(result.affectedRows());
        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Warn(log_context) << "File batch delete failed";
            co_return 0;
        }
    }

    auto DeleteFoldersByIds(
        const drogon::orm::DbClientPtr& client,
        const std::vector<uint64_t>& folder_ids,
        disk::utils::LogContext log_context
    ) -> drogon::Task<int> {
        if (folder_ids.empty()) {
            co_return 0;
        }

        try {
            auto result = co_await client->execSqlCoro(
                "DELETE FROM folders WHERE id IN (" +
                BatchUtils::BuildSafeNumericInClause(folder_ids) + ")"
            );
            co_return static_cast<int>(result.affectedRows());
        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Warn(log_context) << "Folder batch delete failed";
            co_return 0;
        }
    }

    [[nodiscard]] auto NormalizeFulltextKeyword(std::string_view keyword) -> std::string {
        std::string normalized;
        normalized.reserve(keyword.size());

        bool previous_is_space = true;
        for (const auto ch : keyword) {
            if (ch == ' ') {
                if (!previous_is_space) {
                    normalized.push_back(' ');
                }
                previous_is_space = true;
                continue;
            }

            normalized.push_back(ch);
            previous_is_space = false;
        }

        if (!normalized.empty() && normalized.back() == ' ') {
            normalized.pop_back();
        }

        return normalized;
    }

    [[nodiscard]] auto IsFulltextEligible(std::string_view keyword) -> bool {
        const auto normalized = NormalizeFulltextKeyword(keyword);
        if (normalized.size() < 3) {
            return false;
        }

        for (const auto ch : normalized) {
            const auto uch = static_cast<unsigned char>(ch);
            if (uch > 0x7F || (!std::isalnum(uch) && ch != ' ')) {
                return false;
            }
        }

        return normalized.find_first_not_of(' ') != std::string::npos;
    }

    [[nodiscard]] auto ResolveListSortColumn(
        std::string_view sort_by,
        bool folder_only
    ) -> std::string_view {
        if (sort_by == "size") {
            return folder_only ? "sort_size" : "size";
        }
        if (sort_by == "created_at") {
            return "created_at";
        }
        if (sort_by == "updated_at") {
            return "updated_at";
        }
        return "name";
    }

    [[nodiscard]] auto BuildDeterministicOrderByClause(
        std::string_view primary_column,
        std::string_view direction,
        bool include_type,
        std::string_view prefix
    ) -> std::string {
        const auto QualifyColumn = [prefix](std::string_view column) {
            if (prefix.empty()) {
                return std::string(column);
            }

            std::string qualified;
            qualified.reserve(prefix.size() + column.size());
            qualified.append(prefix);
            qualified.append(column);
            return qualified;
        };

        std::string order_by;
        order_by.reserve(96);
        order_by.append(QualifyColumn(primary_column));
        order_by.push_back(' ');
        order_by.append(direction);

        if (primary_column != "name") {
            order_by.append(", ");
            order_by.append(QualifyColumn("name"));
            order_by.push_back(' ');
            order_by.append(direction);
        }

        if (include_type) {
            order_by.append(", ");
            order_by.append(QualifyColumn("type"));
            order_by.push_back(' ');
            order_by.append(direction);
        }

        order_by.append(", ");
        order_by.append(QualifyColumn("id"));
        order_by.push_back(' ');
        order_by.append(direction);
        return order_by;
    }

} ///< namespace disk::file::utils
