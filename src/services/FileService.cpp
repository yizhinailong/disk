/**
 * @file FileService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件上传服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "FileService.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <json/writer.h>

#include "models/FileContents.hpp"
#include "models/Files.hpp"
#include "models/Folders.hpp"
#include "models/Trash.hpp"
#include "models/UploadTaskChunks.hpp"
#include "storage/IFileStorage.hpp"
#include "utils/BatchUtils.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/FileHashUtil.hpp"
#include "utils/StageTimer.hpp"

namespace disk::file {

    using disk::utils::BatchUtils;
    using disk::utils::ConfigMgr;
    using disk::utils::DEFAULT_BATCH_CHUNK_SIZE;
    using disk::utils::FileHashUtil;
    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::FileContents;
    using drogon_model::disk::Files;
    using drogon_model::disk::Folders;
    using drogon_model::disk::Trash;
    using drogon_model::disk::UploadTaskChunks;
    using drogon_model::disk::UploadTasks;

    namespace {
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

        struct ServiceValidationException : std::runtime_error {
            explicit ServiceValidationException(ErrorInfo error_info)
                : std::runtime_error(error_info.message), error(std::move(error_info)) {
            }

            ErrorInfo error;
        };

        [[nodiscard]] auto BuildFilePath(const std::string& folder_path, const std::string& filename)
            -> std::string {
            return folder_path == "/" ? "/" + filename : folder_path + filename;
        }

        [[nodiscard]] auto BuildFolderPath(const std::string& parent_path, const std::string& name)
            -> std::string {
            return parent_path == "/" ? "/" + name + "/" : parent_path + name + "/";
        }

        auto ResolveFolderLocation(
            const drogon::orm::DbClientPtr& client,
            uint64_t folder_id,
            uint64_t user_id
        ) -> drogon::Task<Result<FolderLocation>> {
            if (folder_id == 0) {
                co_return FolderLocation{};
            }

            try {
                auto result = co_await client->execSqlCoro(
                    "SELECT path, depth FROM folders WHERE id = $1 AND user_id = $2",
                    folder_id,
                    user_id
                );
                if (result.empty()) {
                    co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
                }

                co_return FolderLocation{ .path = result[0]["path"].as<std::string>(),
                                          .depth = result[0]["depth"].as<uint32_t>() };
            } catch (const drogon::orm::DrogonDbException& e) {
                Logger::Warn() << "Folder location lookup failed: folder_id=" << folder_id << " - "
                         << e.base().what();
                co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
            }
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
            uint64_t user_id
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
            } catch (const drogon::orm::DrogonDbException& e) {
                Logger::Warn() << "Batch trash insert failed: " << e.base().what();
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

        auto FetchFolderDeletePlan(
            const drogon::orm::DbClientPtr& client,
            uint64_t folder_id,
            uint64_t user_id
        ) -> drogon::Task<std::optional<FolderDeletePlan>> {
            auto folder_result = co_await client->execSqlCoro(
                "WITH RECURSIVE folder_tree AS ( "
                "SELECT id, user_id, parent_id, name, path, depth, item_count, created_at, updated_at "
                "FROM folders WHERE id = $1 AND user_id = $2 "
                "UNION ALL "
                "SELECT f.id, f.user_id, f.parent_id, f.name, f.path, f.depth, f.item_count, f.created_at, f.updated_at "
                "FROM folders f INNER JOIN folder_tree ft ON f.parent_id = ft.id "
                "WHERE f.user_id = $2 "
                ") SELECT id, user_id, parent_id, name, path, depth, item_count, created_at, updated_at "
                "FROM folder_tree ORDER BY depth ASC, id ASC",
                folder_id,
                user_id,
                user_id
            );

            if (folder_result.empty()) {
                co_return std::nullopt;
            }

            FolderDeletePlan plan;
            plan.folders.reserve(folder_result.size());
            for (const auto& row : folder_result) {
                plan.folders.emplace_back(row, -1);
            }
            plan.root = plan.folders.front();

            std::vector<uint64_t> folder_ids;
            folder_ids.reserve(plan.folders.size());
            for (const auto& folder : plan.folders) {
                folder_ids.push_back(folder.getValueOfId());
            }

            auto file_result = co_await client->execSqlCoro(
                "SELECT id, user_id, folder_id, content_id, name, extension, size, mime_type, path, "
                "is_favorite, download_count, last_accessed_at, created_at, updated_at "
                "FROM files WHERE user_id = $1 AND folder_id IN (" +
                    BatchUtils::BuildSafeNumericInClause(folder_ids) + ") ORDER BY folder_id ASC, id ASC",
                user_id
            );

            plan.files.reserve(file_result.size());
            for (const auto& row : file_result) {
                plan.files.emplace_back(row, -1);
                plan.item_size += plan.files.back().getValueOfSize();
            }

            co_return plan;
        }

        auto FetchBatchFolderDeletePlans(
            const drogon::orm::DbClientPtr& client,
            const std::vector<uint64_t>& folder_ids,
            uint64_t user_id
        ) -> drogon::Task<std::unordered_map<uint64_t, FolderDeletePlan>> {
            std::unordered_map<uint64_t, FolderDeletePlan> plans;

            if (folder_ids.empty()) {
                co_return plans;
            }

            auto folder_result = co_await client->execSqlCoro(
                "WITH RECURSIVE folder_tree AS ( "
                "SELECT id, user_id, parent_id, name, path, depth, item_count, created_at, updated_at, "
                "id AS root_id "
                "FROM folders WHERE id IN (" +
                    BatchUtils::BuildSafeNumericInClause(folder_ids) + ") AND user_id = $1 "
                "UNION ALL "
                "SELECT f.id, f.user_id, f.parent_id, f.name, f.path, f.depth, f.item_count, f.created_at, f.updated_at, "
                "ft.root_id "
                "FROM folders f INNER JOIN folder_tree ft ON f.parent_id = ft.id "
                "WHERE f.user_id = $1 "
                ") SELECT id, user_id, parent_id, name, path, depth, item_count, created_at, updated_at, root_id "
                "FROM folder_tree ORDER BY root_id ASC, depth ASC, id ASC",
                user_id
            );

            std::unordered_map<uint64_t, size_t> root_to_plan_index;
            std::vector<uint64_t> all_folder_ids;

            for (const auto& row : folder_result) {
                auto root_id = row["root_id"].as<uint64_t>();
                auto folder_id = row["id"].as<uint64_t>();

                auto it = root_to_plan_index.find(root_id);
                if (it == root_to_plan_index.end()) {
                    FolderDeletePlan plan;
                    plan.folders.emplace_back(row, -1);
                    plan.root = plan.folders.front();
                    root_to_plan_index[root_id] = plans.size();
                    plans.emplace(root_id, std::move(plan));
                } else {
                    auto plan_it = plans.find(root_id);
                    plan_it->second.folders.emplace_back(row, -1);
                }

                all_folder_ids.push_back(folder_id);
            }

            if (all_folder_ids.empty()) {
                co_return plans;
            }

            std::sort(all_folder_ids.begin(), all_folder_ids.end());
            all_folder_ids.erase(std::unique(all_folder_ids.begin(), all_folder_ids.end()), all_folder_ids.end());

            auto file_result = co_await client->execSqlCoro(
                "SELECT id, user_id, folder_id, content_id, name, extension, size, mime_type, path, "
                "is_favorite, download_count, last_accessed_at, created_at, updated_at "
                "FROM files WHERE user_id = $1 AND folder_id IN (" +
                    BatchUtils::BuildSafeNumericInClause(all_folder_ids) + ") ORDER BY folder_id ASC, id ASC",
                user_id
            );

            std::unordered_map<uint64_t, std::vector<uint64_t>> root_to_folder_ids;
            for (const auto& [root_id, plan] : plans) {
                auto& ids = root_to_folder_ids[root_id];
                ids.reserve(plan.folders.size());
                for (const auto& folder : plan.folders) {
                    ids.push_back(folder.getValueOfId());
                }
                std::sort(ids.begin(), ids.end());
            }

            for (const auto& row : file_result) {
                auto folder_id = row["folder_id"].as<uint64_t>();
                uint64_t matched_root = 0;
                for (const auto& [root_id, folder_id_list] : root_to_folder_ids) {
                    if (std::binary_search(folder_id_list.begin(), folder_id_list.end(), folder_id)) {
                        matched_root = root_id;
                        break;
                    }
                }
                if (matched_root == 0) continue;

                auto plan_it = plans.find(matched_root);
                if (plan_it != plans.end()) {
                    plan_it->second.files.emplace_back(row, -1);
                    plan_it->second.item_size += plan_it->second.files.back().getValueOfSize();
                }
            }

            co_return plans;
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

        auto DeleteFoldersByIds(
            const drogon::orm::DbClientPtr& client,
            const std::vector<uint64_t>& folder_ids
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
            } catch (const drogon::orm::DrogonDbException& e) {
                Logger::Warn() << "Batch folder delete failed: " << e.base().what();
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
            std::string_view prefix = {}
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
    } ///< namespace

    /// ==================== 构造函数 ====================

    FileService::FileService(drogon::orm::DbClientPtr db_client, storage::IFileStorage* storage)
        : m_db_client(std::move(db_client)), m_storage(storage) {
        StartUploadTaskCacheMaintenance();
        Logger::Debug() << "FileService initialization completed";
    }

    /// ==================== InitUpload ====================

    auto FileService::InitUpload(InitUploadRequest request, uint64_t user_id)
        -> drogon::Task<Result<InitUploadResponse>> {

        Logger::Debug() << "Starting initialize upload: filename=\"" << request.filename
                  << "\", file_size=" << request.file_size << ", file_hash=" << request.file_hash
                  << ", parent_id=" << request.parent_id << ", user_id=" << user_id;

        auto config = ConfigMgr::GetInstance();
        auto max_file_size = config->GetMaxFileSize();
        if (request.file_size > max_file_size) {
            Logger::Warn() << "Upload file exceeds max size: filename=\"" << request.filename
                     << "\", file_size=" << request.file_size
                     << ", max_file_size=" << max_file_size << ", user_id=" << user_id;
            co_return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "File size exceeds maximum allowed size")
            );
        }

        /// 1. Compound pre-check: folder location + filename collision + instant upload + resume
        auto combined = co_await m_db_client->execSqlCoro(
            "WITH folder_loc AS ("
            "  SELECT path, depth FROM folders WHERE id = $1 AND user_id = $2"
            "), filename_exists AS ("
            "  SELECT COUNT(*) AS cnt FROM files"
            "  WHERE user_id = $2 AND folder_id = $1 AND name = $3"
            "), existing_content AS ("
            "  SELECT id, mime_type FROM file_contents WHERE hash_md5 = $4 LIMIT 1"
            "), existing_task AS ("
            "  SELECT id FROM upload_tasks"
            "  WHERE user_id = $2 AND file_hash = $4 AND status = 0 LIMIT 1"
            ")"
            " SELECT"
            "   (SELECT path FROM folder_loc) AS folder_path,"
            "   (SELECT depth FROM folder_loc) AS folder_depth,"
            "   (SELECT cnt FROM filename_exists) AS filename_count,"
            "   (SELECT id FROM existing_content) AS content_id,"
            "   (SELECT mime_type FROM existing_content) AS content_mime_type,"
            "   (SELECT id FROM existing_task) AS task_id",
            request.parent_id,
            user_id,
            request.filename,
            request.file_hash
        );

        const auto& row = combined[0];

        /// Validate folder location
        if (request.parent_id != 0) {
            auto folder_path = row["folder_path"].isNull() ? std::string{} : row["folder_path"].as<std::string>();
            if (row["folder_path"].isNull()) {
                Logger::Warn() << "Folder not found: parent_id=" << request.parent_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
            }
        }

        /// Check filename collision
        auto filename_count = row["filename_count"].as<int64_t>();
        if (filename_count > 0) {
            Logger::Warn() << "File with same name already exists during upload init: "
                     << request.filename << ", parent_id=" << request.parent_id
                     << ", user_id=" << user_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::FileAlreadyExists));
        }

        /// Detect instant upload
        auto content_id = row["content_id"].isNull() ? uint64_t{0} : row["content_id"].as<uint64_t>();
        auto content_mime_type = row["content_mime_type"].isNull() ? std::string{} : row["content_mime_type"].as<std::string>();

        if (content_id != 0) {
            Logger::Debug() << "Instant upload check successful: file_hash=" << request.file_hash
                      << ", content_id=" << content_id;

            std::shared_ptr<drogon::orm::Transaction> transaction;
            try {
                transaction = co_await m_db_client->newTransactionCoro();

                /// 在事务内检查同名文件
                if (co_await IsFilenameExists(
                        transaction,
                        request.parent_id,
                        request.filename,
                        user_id
                    )) {
                    Logger::Warn() << "File with same name already exists: " << request.filename;
                    co_return std::unexpected(ErrorInfo(ErrorCode::FileAlreadyExists));
                }

                /// 事务内递增引用计数
                auto increment_result = co_await transaction->execSqlCoro(
                    "UPDATE file_contents SET ref_count = ref_count + 1 WHERE id = $1",
                    content_id
                );
                if (increment_result.affectedRows() == 0) {
                    Logger::Warn() << "File content not found for instant upload: content_id="
                             << content_id;
                    throw std::runtime_error("Failed to increment file content reference count");
                }

                auto parent_location_result =
                    co_await ResolveFolderLocation(transaction, request.parent_id, user_id);
                if (!parent_location_result) {
                    co_return std::unexpected(parent_location_result.error());
                }

                /// 创建文件记录（使用 compound query 提供的 mime_type，无需二次读取）
                Files file;
                file.setUserId(user_id);
                file.setContentId(content_id);
                file.setFolderId(request.parent_id);
                file.setName(request.filename);
                file.setExtension(ExtractExtension(request.filename));
                file.setSize(request.file_size);
                file.setMimeType(content_mime_type);
                file.setPath(BuildFilePath(parent_location_result->path, request.filename));
                file.setIsFavorite(0);
                file.setDownloadCount(0);

                CoroMapper<Files> file_mapper(transaction);
                file = co_await file_mapper.insert(file);

                /// 注：秒传时 storage_used 不增加，因为物理文件已存在

                /// 构造响应
                InitUploadResponse response;
                response.instant_upload = true;
                response.file =
                    FileItem{ .id = file.getValueOfId(),
                              .name = file.getValueOfName(),
                              .size = file.getValueOfSize(),
                              .hash = request.file_hash,
                              .mime_type = file.getValueOfMimeType(),
                              .parent_id = file.getValueOfFolderId(),
                              .created_at = file.getValueOfCreatedAt().toDbStringLocal() };

                Logger::Debug() << "Instant upload completed: file_id=" << file.getValueOfId();
                co_return response;

            } catch (const drogon::orm::DrogonDbException& e) {
                Logger::Error() << "Instant upload create file record failed: " << e.base().what();
                if (transaction) {
                    try {
                        transaction->rollback();
                    } catch (const std::exception& rollback_e) {
                        Logger::Error() << "Transaction rollback failed: " << rollback_e.what();
                    }
                }
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to create file record")
                );
            } catch (const std::exception& e) {
                Logger::Error() << "Instant upload create file record failed: " << e.what();
                if (transaction) {
                    try {
                        transaction->rollback();
                    } catch (const std::exception& rollback_e) {
                        Logger::Error() << "Transaction rollback failed: " << rollback_e.what();
                    }
                }
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to create file record")
                );
            }
        }

        /// 2. 检测断点续传 (only when task_id is not NULL)
        auto task_id = row["task_id"].isNull() ? std::string{} : row["task_id"].as<std::string>();
        if (!task_id.empty()) {
            auto existing_task = co_await FindExistingTask(user_id, request.file_hash);
            if (existing_task.has_value()) {
                const auto& task = existing_task.value();
                const auto& existing_task_id = task.getValueOfId();

                if (task.getValueOfExpiresAt() < trantor::Date::now()) {
                    Logger::Info() << "Expired upload task found, discarding: upload_id=" << existing_task_id;
                    InvalidateUploadTaskCache(existing_task_id);

                    try {
                        co_await m_db_client->execSqlCoro(
                            "DELETE FROM upload_tasks WHERE id = $1 AND status = 0",
                            existing_task_id
                        );
                    } catch (const drogon::orm::DrogonDbException& e) {
                        Logger::Warn() << "Failed to delete expired upload task: " << e.base().what();
                    }

                    auto cleanup_result = co_await m_storage->CleanupTemp(existing_task_id);
                    if (!cleanup_result) {
                        Logger::Warn() << "Failed to cleanup temp for expired task: upload_id=" << existing_task_id;
                    }
                } else {
                    Logger::Debug() << "Resume upload check successful: upload_id=" << existing_task_id;
                    InvalidateUploadTaskCache(existing_task_id);

                    auto chunk_result = co_await m_db_client->execSqlCoro(
                        "SELECT chunk_index FROM upload_task_chunks WHERE task_id = $1 ORDER BY chunk_index",
                        existing_task_id
                    );

                    InitUploadResponse response;
                    response.upload_id = existing_task_id;
                    response.chunk_size = task.getValueOfChunkSize();
                    response.total_chunks = task.getValueOfTotalChunks();
                    response.instant_upload = false;

                    response.uploaded_chunks.clear();
                    for (const auto& chunk_row : chunk_result) {
                        response.uploaded_chunks.push_back(chunk_row["chunk_index"].as<uint32_t>());
                    }

                    co_return response;
                }
            }
        }

        /// 3. 预留存储配额
        auto quota_result = co_await ReserveStorageQuota(user_id, request.file_size);
        if (!quota_result) {
            Logger::Warn() << "Storage quota reservation failed: user_id=" << user_id;
            co_return std::unexpected(quota_result.error());
        }

        /// 4. 创建新的上传任务
        auto chunk_size = config->GetChunkSize();
        if (chunk_size == 0) {
            Logger::Error() << "Invalid upload chunk size configured: 0";
            co_await ReleaseReservedQuota(user_id, request.file_size);
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Invalid upload chunk size configuration")
            );
        }

        const auto total_chunks_u64 = ((request.file_size - 1) / chunk_size) + 1;
        if (total_chunks_u64 > std::numeric_limits<uint32_t>::max()) {
            Logger::Warn() << "Upload requires too many chunks: filename=\"" << request.filename
                     << "\", file_size=" << request.file_size
                     << ", chunk_size=" << chunk_size
                     << ", total_chunks=" << total_chunks_u64;
            co_await ReleaseReservedQuota(user_id, request.file_size);
            co_return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "File requires too many chunks")
            );
        }
        auto total_chunks = static_cast<uint32_t>(total_chunks_u64);
        auto expiry_seconds = config->GetUploadTaskExpirySeconds();

        /// 生成上传 ID
        auto upload_id = drogon::utils::getUuid();

        UploadTasks task;
        task.setId(upload_id);
        task.setUserId(user_id);
        task.setFolderId(request.parent_id);
        task.setFilename(request.filename);
        task.setFileSize(request.file_size);
        task.setFileHash(request.file_hash);
        task.setChunkSize(chunk_size);
        task.setTotalChunks(total_chunks);
        task.setReservedBytes(request.file_size);
        task.setTempPath(upload_id);
        task.setStatus(0); ///< 进行中
        task.setExpiresAt(trantor::Date::now().after(expiry_seconds));

        bool create_task_failed = false;
        try {
            CoroMapper<UploadTasks> mapper(m_db_client);
            task = co_await mapper.insert(task);

            Logger::Debug() << "Upload task created successfully: upload_id=" << task.getValueOfId()
                      << ", total_chunks=" << total_chunks;
            InvalidateUploadTaskCache(task.getValueOfId());

            /// 预创建临时上传目录，避免每个分片写入时重复创建
            auto ensure_result = co_await m_storage->EnsureUploadTempDir(task.getValueOfId());
            if (!ensure_result) {
                Logger::Warn() << "Failed to ensure upload temp directory: upload_id="
                         << task.getValueOfId();
            }

            InitUploadResponse response;
            response.upload_id = task.getValueOfId();
            response.chunk_size = task.getValueOfChunkSize();
            response.total_chunks = task.getValueOfTotalChunks();
            response.uploaded_chunks = {};
            response.instant_upload = false;

            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to create upload task: " << e.base().what();
            create_task_failed = true;
        }

        if (create_task_failed) {
            co_await ReleaseReservedQuota(user_id, request.file_size);
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to create upload task")
            );
        }

        co_return std::unexpected(
            ErrorInfo(ErrorCode::InternalError, "Unexpected upload initialization state")
        );
    }

    /// ==================== UploadChunk ====================

    auto FileService::UploadChunk(
        std::string upload_id,
        uint32_t chunk_index,
        std::string chunk_hash,
        std::string_view chunk_data,
        uint64_t user_id
    ) -> drogon::Task<Result<UploadChunkResponse>> {

        auto start = std::chrono::steady_clock::now();

        Logger::Debug() << "Starting upload chunk: upload_id=" << upload_id
                  << ", chunk_index=" << chunk_index << ", chunk_hash=" << chunk_hash
                  << ", data_size=" << chunk_data.size();

        /// 1. 优先读取短 TTL 上传任务缓存，命中后避免重复查询数据库
        auto cached_task = TryGetUploadTaskCacheEntry(upload_id, user_id);
        if (!cached_task.has_value()) {
            auto task_result = co_await FindUploadTask(upload_id, user_id);
            if (!task_result) {
                Logger::Warn() << "Upload task verification failed: " << upload_id;

                auto end = std::chrono::steady_clock::now();
                auto duration_us =
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                Logger::Info() << "[upload_chunk] duration_us=" << duration_us
                         << " outcome=failure upload_id=" << upload_id
                         << " chunk_index=" << chunk_index
                         << " data_size=" << chunk_data.size();

                co_return std::unexpected(task_result.error());
            }

            auto cache_entry = BuildUploadTaskCacheEntry(task_result.value());
            CacheUploadTaskEntry(upload_id, cache_entry);
            cached_task = std::move(cache_entry);
        }

        const auto& task = cached_task.value();

        /// 2. 验证任务未过期
        if (task.expires_at < trantor::Date::now()) {
            Logger::Warn() << "Upload task expired: " << upload_id;
            InvalidateUploadTaskCache(upload_id);

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[upload_chunk] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " chunk_index=" << chunk_index
                     << " data_size=" << chunk_data.size();

            co_return std::unexpected(
                ErrorInfo(ErrorCode::UploadTaskNotFound, "Upload task expired")
            );
        }

        /// 3. 验证分片索引有效
        if (chunk_index >= task.total_chunks) {
            Logger::Warn() << "Chunk index out of range: chunk_index=" << chunk_index
                     << ", total_chunks=" << task.total_chunks;

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[upload_chunk] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " chunk_index=" << chunk_index
                     << " data_size=" << chunk_data.size();

            co_return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Chunk index out of range")
            );
        }

        /// 4. 验证分片大小符合任务几何信息
        const auto chunk_offset = static_cast<uint64_t>(chunk_index) * task.chunk_size;
        const auto remaining_bytes = task.file_size - chunk_offset;
        const auto expected_size = std::min<uint64_t>(task.chunk_size, remaining_bytes);
        if (chunk_data.size() != expected_size) {
            Logger::Warn() << "Unexpected chunk size: upload_id=" << upload_id
                     << ", chunk_index=" << chunk_index
                     << ", expected_size=" << expected_size
                     << ", actual_size=" << chunk_data.size()
                     << ", file_size=" << task.file_size
                     << ", chunk_size=" << task.chunk_size;

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[upload_chunk] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " chunk_index=" << chunk_index
                     << " data_size=" << chunk_data.size();

            co_return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Unexpected chunk size")
            );
        }

        /// 5. 将请求体复制到拥有所有权的缓冲区，只做一次哈希+落盘复用。
        std::string chunk_payload{ chunk_data };
        auto actual_hash = FileHashUtil::HashMd5(chunk_payload);
        if (actual_hash != chunk_hash) {
            Logger::Warn() << "Chunk hash mismatch: expected=" << chunk_hash
                     << ", actual=" << actual_hash;

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[upload_chunk] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " chunk_index=" << chunk_index
                     << " data_size=" << chunk_data.size();

            co_return std::unexpected(
                ErrorInfo(ErrorCode::ChunkVerifyFailed, "Chunk hash mismatch")
            );
        }

        /// 6. 创建临时目录并写入分片
        auto write_result = co_await m_storage->WriteChunk(upload_id, chunk_index, std::move(chunk_payload));
        if (!write_result) {
            Logger::Error() << "Failed to write chunk file: upload_id=" << upload_id
                      << ", chunk_index=" << chunk_index << ", error="
                      << static_cast<int>(write_result.error().code);

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[upload_chunk] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " chunk_index=" << chunk_index
                     << " data_size=" << chunk_data.size();

            co_return std::unexpected(write_result.error());
        }

        /// 7. 记录已上传分片（幂等：INSERT IGNORE 允许重复上传同一分片）
        try {
            co_await m_db_client->execSqlCoro(
                "INSERT INTO upload_task_chunks (task_id, chunk_index, uploaded_at) VALUES ($1, $2, NOW()) ON CONFLICT DO NOTHING",
                upload_id,
                chunk_index
            );

            Logger::Debug() << "Chunk upload successful: upload_id=" << upload_id
                      << ", chunk_index=" << chunk_index;

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Debug() << "[upload_chunk] duration_us=" << duration_us
                      << " outcome=success upload_id=" << upload_id
                      << " chunk_index=" << chunk_index
                      << " data_size=" << chunk_data.size();

            UploadChunkResponse response;
            response.chunk_index = chunk_index;
            response.uploaded = true;

            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to record chunk upload: " << e.base().what();

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[upload_chunk] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " chunk_index=" << chunk_index
                     << " data_size=" << chunk_data.size();

            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to record chunk upload")
            );
        }
    }

    /// ==================== CompleteUpload ====================

    auto FileService::CompleteUpload(std::string upload_id, uint64_t user_id)
        -> drogon::Task<Result<CompleteUploadResponse>> {

        auto start = std::chrono::steady_clock::now();

        Logger::Debug() << "Starting complete upload: upload_id=" << upload_id << ", user_id=" << user_id;

        /// 1. 查找并验证上传任务
        auto task_result = co_await FindUploadTask(upload_id, user_id);
        if (!task_result) {
            Logger::Warn() << "Upload task verification failed: " << upload_id;

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[complete_upload] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id;

            co_return std::unexpected(task_result.error());
        }

        auto task = task_result.value();

        /// 2. Check idempotency: already completed
        if (task.getValueOfStatus() == 1) {
            Logger::Debug() << "Upload task already completed: upload_id=" << upload_id;
            InvalidateUploadTaskCache(upload_id);

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Debug() << "[complete_upload] duration_us=" << duration_us
                      << " outcome=success upload_id=" << upload_id
                      << " total_chunks=" << task.getValueOfTotalChunks();

            co_return CompleteUploadResponse{};
        }

        /// 3. 单次聚合查询校验分片完整性
        auto chunk_scan_start = std::chrono::steady_clock::now();
        const auto LogChunkScanDuration = [&chunk_scan_start, &upload_id]() {
            Logger::Debug() << "[stage_timer] chunk_scan duration_ms="
                      << std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - chunk_scan_start
                         )
                             .count()
                      << " upload_id=" << upload_id;
        };

        auto coverage_result = co_await GetUploadedChunkCoverage(m_db_client, upload_id);
        if (!coverage_result.has_value()) {
            LogChunkScanDuration();
            Logger::Error() << "Failed to query chunk coverage: upload_id=" << upload_id;

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[complete_upload] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " total_chunks=" << task.getValueOfTotalChunks();

            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to query chunk coverage")
            );
        }

        const auto& coverage = coverage_result.value();
        const auto total_chunks = task.getValueOfTotalChunks();

        bool chunks_valid = false;
        if (total_chunks == 0) {
            /// 零分片文件：不期望任何已上传分片
            chunks_valid = (coverage.uploaded_count == 0);
        } else {
            /// 非零分片：数量和最大索引必须匹配
            chunks_valid = (coverage.uploaded_count == static_cast<uint64_t>(total_chunks)) &&
                           (coverage.max_chunk_index == static_cast<int64_t>(total_chunks - 1));
        }

        LogChunkScanDuration();

        if (!chunks_valid) {
            Logger::Warn() << "Not all chunks uploaded: uploaded=" << coverage.uploaded_count
                     << ", total=" << total_chunks
                     << ", max_index=" << coverage.max_chunk_index;

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[complete_upload] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " total_chunks=" << total_chunks;

            co_return std::unexpected(
                ErrorInfo(ErrorCode::ValidationFailed, "Not all chunks uploaded")
            );
        }

        /// 3. 组装分片（同时计算 MD5 + SHA256）
        auto assemble_start = std::chrono::steady_clock::now();
        auto assemble_result = co_await m_storage->AssembleChunks(upload_id, task.getValueOfTotalChunks());
        Logger::Debug() << "[stage_timer] assemble duration_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - assemble_start
                     )
                         .count()
                  << " upload_id=" << upload_id
                  << " total_chunks=" << task.getValueOfTotalChunks();
        if (!assemble_result) {
            Logger::Error() << "Failed to assemble chunks: upload_id=" << upload_id
                      << ", error=" << static_cast<int>(assemble_result.error().code);

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[complete_upload] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " total_chunks=" << task.getValueOfTotalChunks();

            co_return std::unexpected(assemble_result.error());
        }
        const auto& assembled = assemble_result.value();
        const auto& assemble_path = assembled.path;

        /// 4. 使用组装时计算的哈希值
        const auto& final_hash = assembled.md5_hash;
        const auto& precomputed_sha256 = assembled.sha256_hash;
        if (final_hash != task.getValueOfFileHash()) {
            Logger::Error() << "File hash mismatch: expected=" << task.getValueOfFileHash()
                      << ", actual=" << final_hash;
            auto delete_result = co_await m_storage->DeletePath(assemble_path);
            if (!delete_result) {
                Logger::Warn() << "Failed to cleanup assemble file after hash mismatch: "
                         << static_cast<int>(delete_result.error().code);
            }

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[complete_upload] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " total_chunks=" << task.getValueOfTotalChunks();

            co_return std::unexpected(
                ErrorInfo(ErrorCode::ChunkVerifyFailed, "File hash verification failed")
            );
        }

        Logger::Debug() << "File hash verification passed: " << final_hash;

        struct FinalizeLookupResult {
            std::optional<uint64_t> existing_content_id;
            bool filename_exists = false;
        };

        auto dedup_start = std::chrono::steady_clock::now();
        auto lookup_result = co_await [this,
                                       &final_hash,
                                       &task,
                                       user_id]() -> drogon::Task<FinalizeLookupResult> {
            try {
                auto result = co_await m_db_client->execSqlCoro(
                    "SELECT (SELECT id FROM file_contents WHERE hash_md5 = $1 LIMIT 1) AS content_id, " "EXISTS(SELECT 1 FROM files WHERE user_id = $2 AND folder_id = $3 AND name = $4) AS filename_exists",
                    final_hash,
                    user_id,
                    task.getValueOfFolderId(),
                    task.getValueOfFilename()
                );

                FinalizeLookupResult lookup;
                if (!result.empty()) {
                    if (!result[0]["content_id"].isNull()) {
                        lookup.existing_content_id = result[0]["content_id"].as<uint64_t>();
                    }
                    lookup.filename_exists = result[0]["filename_exists"].as<int>() != 0;
                }

                co_return lookup;
            } catch (const drogon::orm::DrogonDbException& e) {
                Logger::Error() << "Failed to query finalize upload metadata: " << e.base().what();
                co_return FinalizeLookupResult{};
            }
        }();

        if (lookup_result.filename_exists) {
            Logger::Warn() << "File with same name already exists: " << task.getValueOfFilename();
            auto delete_result = co_await m_storage->DeletePath(assemble_path);
            if (!delete_result) {
                Logger::Warn() << "Failed to cleanup assemble file on duplicate name: "
                         << static_cast<int>(delete_result.error().code);
            }

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[complete_upload] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " total_chunks=" << task.getValueOfTotalChunks();

            Logger::Info() << "[stage_timer] dedup_lookup duration_ms="
                     << std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - dedup_start
                        )
                            .count()
                     << " upload_id=" << upload_id
                     << " dedup_hit=" << (lookup_result.existing_content_id.has_value() ? "true" : "false")
                     << " filename_exists=true";

            co_return std::unexpected(ErrorInfo(ErrorCode::FileAlreadyExists));
        }

        auto existing_content = lookup_result.existing_content_id;
        std::filesystem::path final_storage_path;
        std::string final_sha256;
        bool should_compensate_storage_file = false;

        if (existing_content.has_value()) {
            auto delete_result = co_await m_storage->DeletePath(assemble_path);
            if (!delete_result) {
                Logger::Warn() << "Failed to cleanup assemble file after dedup: "
                         << static_cast<int>(delete_result.error().code);
            }
            Logger::Debug() << "File dedup successful: content_id=" << existing_content.value();
        } else {
            auto promote_result = co_await m_storage->PromoteToFinal(assemble_path, final_hash);
            if (!promote_result) {
                Logger::Error() << "Failed to move file to final storage: error="
                          << static_cast<int>(promote_result.error().code);
                auto cleanup_result = co_await m_storage->DeletePath(assemble_path);
                if (!cleanup_result) {
                    Logger::Warn() << "Failed to cleanup assemble file after promote failure: "
                             << static_cast<int>(cleanup_result.error().code);
                }

                auto end = std::chrono::steady_clock::now();
                auto duration_us =
                    std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                Logger::Info() << "[complete_upload] duration_us=" << duration_us
                         << " outcome=failure upload_id=" << upload_id
                         << " total_chunks=" << task.getValueOfTotalChunks();

                co_return std::unexpected(promote_result.error());
            }

            final_storage_path = promote_result.value();
            final_sha256 = precomputed_sha256;
            should_compensate_storage_file = true;
        }
        Logger::Debug() << "[stage_timer] dedup_lookup duration_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - dedup_start
                     )
                         .count()
                  << " upload_id=" << upload_id
                  << " dedup_hit=" << (existing_content.has_value() ? "true" : "false")
                  << " filename_exists=false";

        std::shared_ptr<drogon::orm::Transaction> transaction;
        Files file;
        bool db_operation_failed = false;
        auto tx_start = std::chrono::steady_clock::now();
        try {
            transaction = co_await m_db_client->newTransactionCoro();

            CoroMapper<FileContents> content_mapper(transaction);
            CoroMapper<Files> file_mapper(transaction);

            uint64_t content_id = 0;
            if (existing_content.has_value()) {
                content_id = existing_content.value();
                auto increment_result = co_await transaction->execSqlCoro(
                    "UPDATE file_contents SET ref_count = ref_count + 1 WHERE id = $1",
                    content_id
                );
                if (increment_result.affectedRows() == 0) {
                    Logger::Warn() << "File content not found when finalizing upload: content_id="
                             << content_id;
                    throw std::runtime_error("Failed to increment file content reference count");
                }
            } else {
                FileContents content;
                content.setHashMd5(final_hash);
                content.setHashSha256(final_sha256);
                content.setSize(task.getValueOfFileSize());
                content.setStoragePath(final_storage_path.string());
                content.setMimeType("");
                content.setRefCount(1);

                content = co_await content_mapper.insert(content);
                content_id = content.getValueOfId();
                Logger::Debug() << "FileContents created successfully: content_id=" << content_id;
            }

            auto parent_location_result =
                co_await ResolveFolderLocation(transaction, task.getValueOfFolderId(), user_id);
            if (!parent_location_result) {
                throw std::runtime_error("Target upload folder not found");
            }

            file.setUserId(user_id);
            file.setContentId(content_id);
            file.setFolderId(task.getValueOfFolderId());
            file.setName(task.getValueOfFilename());
            file.setExtension(ExtractExtension(task.getValueOfFilename()));
            file.setSize(task.getValueOfFileSize());
            file.setMimeType("");
            file.setPath(BuildFilePath(parent_location_result->path, task.getValueOfFilename()));
            file.setIsFavorite(0);
            file.setDownloadCount(0);

            file = co_await file_mapper.insert(file);

                auto transfer_result = co_await transaction->execSqlCoro(
                    "UPDATE users SET storage_reserved = GREATEST(storage_reserved - $1, 0), " "storage_used = storage_used + $2 WHERE id = $3",
                    task.getValueOfFileSize(),
                    task.getValueOfFileSize(),
                    user_id
                );

            if (transfer_result.affectedRows() == 0) {
                throw std::runtime_error("Failed to transfer reserved quota to used");
            }

            auto finalize_result = co_await transaction->execSqlCoro(
                "UPDATE upload_tasks SET status = 1, finalized_at = NOW() WHERE id = $1 AND status = 0",
                upload_id
            );
            if (finalize_result.affectedRows() == 0) {
                throw std::runtime_error("Failed to finalize upload task");
            }

            co_await transaction->execSqlCoro(
                "DELETE FROM upload_task_chunks WHERE task_id = $1",
                upload_id
            );

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Database operation failed: " << e.base().what();
            if (transaction) {
                try {
                    transaction->rollback();
                } catch (const std::exception& rollback_e) {
                    Logger::Error() << "Transaction rollback failed: " << rollback_e.what();
                }
            }
            db_operation_failed = true;
        } catch (const std::exception& e) {
            Logger::Error() << "Database operation failed: " << e.what();
            if (transaction) {
                try {
                    transaction->rollback();
                } catch (const std::exception& rollback_e) {
                    Logger::Error() << "Transaction rollback failed: " << rollback_e.what();
                }
            }
            db_operation_failed = true;
        }
        Logger::Debug() << "[stage_timer] tx duration_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - tx_start
                     )
                         .count()
                  << " upload_id=" << upload_id
                  << " success=" << (!db_operation_failed ? "true" : "false");

        if (db_operation_failed) {
            auto compensation_start = std::chrono::steady_clock::now();
            if (should_compensate_storage_file) {
                auto cleanup_result = co_await m_storage->DeletePath(final_storage_path);
                if (!cleanup_result) {
                    Logger::Error() << "Compensation failed, orphan storage file may remain: "
                              << final_storage_path;
                }
            }
            Logger::Info() << "[stage_timer] compensation_cleanup duration_ms="
                     << std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - compensation_start
                        )
                            .count()
                     << " upload_id=" << upload_id;

            auto end = std::chrono::steady_clock::now();
            auto duration_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            Logger::Info() << "[complete_upload] duration_us=" << duration_us
                     << " outcome=failure upload_id=" << upload_id
                     << " total_chunks=" << task.getValueOfTotalChunks();

            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Database operation failed")
            );
        }

        Logger::Debug() << "Files record created successfully: file_id=" << file.getValueOfId();
        InvalidateUploadTaskCache(upload_id);

        /// 7. Cleanup temp directory
        auto temp_cleanup_start = std::chrono::steady_clock::now();
        auto cleanup_result = co_await m_storage->CleanupTemp(upload_id);
        Logger::Debug() << "[stage_timer] temp_cleanup duration_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - temp_cleanup_start
                     )
                         .count()
                  << " upload_id=" << upload_id;
        if (!cleanup_result) {
            Logger::Warn() << "Failed to cleanup temp artifacts: "
                     << static_cast<int>(cleanup_result.error().code);
        }

        CompleteUploadResponse response;
        response.file = FileItem{ .id = file.getValueOfId(),
                                  .name = file.getValueOfName(),
                                  .size = file.getValueOfSize(),
                                  .hash = final_hash,
                                  .mime_type = file.getValueOfMimeType(),
                                  .parent_id = file.getValueOfFolderId(),
                                  .created_at = file.getValueOfCreatedAt().toDbStringLocal() };

        Logger::Debug() << "File upload completed: file_id=" << file.getValueOfId()
                  << ", filename=" << task.getValueOfFilename();

        auto end = std::chrono::steady_clock::now();
        auto duration_us =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        Logger::Debug() << "[complete_upload] duration_us=" << duration_us
                  << " outcome=success upload_id=" << upload_id
                  << " total_chunks=" << task.getValueOfTotalChunks();

        co_return response;
    }

    auto FileService::CancelUpload(std::string upload_id, uint64_t user_id)
        -> drogon::Task<Result<void>> {

        Logger::Debug() << "Starting cancel upload: upload_id=" << upload_id << ", user_id=" << user_id;

        /// 1. 查找并验证上传任务
        auto task_result = co_await FindUploadTask(upload_id, user_id);
        if (!task_result) {
            Logger::Warn() << "Upload task verification failed: " << upload_id;
            co_return std::unexpected(task_result.error());
        }
        const auto task = task_result.value();

        /// 2. Check idempotency: already in terminal state
        if (task.getValueOfStatus() != 0) {
            Logger::Debug() << "Upload task already in terminal state: upload_id=" << upload_id
                      << ", status=" << task.getValueOfStatus();
            co_return {};
        }

        /// 3. Release reserved quota
        co_await ReleaseReservedQuota(user_id, task.getValueOfReservedBytes());

        /// 4. Set terminal state (status=2 cancelled)
        try {
            co_await m_db_client->execSqlCoro(
                "UPDATE upload_tasks SET status = 2, finalized_at = NOW(), " "fail_reason = '用户取消' WHERE id = $1 AND status = 0",
                upload_id
            );
            InvalidateUploadTaskCache(upload_id);
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to set cancel terminal state: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to cancel upload task")
            );
        }

        /// 5. Cleanup chunk tracking rows
        try {
            co_await m_db_client->execSqlCoro(
                "DELETE FROM upload_task_chunks WHERE task_id = $1",
                upload_id
            );
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "Failed to cleanup upload_task_chunks: " << e.base().what();
        }

        /// 6. Cleanup temp directory
        auto cleanup_result = co_await m_storage->CleanupTemp(upload_id);
        if (!cleanup_result) {
            Logger::Warn() << "Failed to delete temp directory: upload_id=" << upload_id
                     << ", error=" << static_cast<int>(cleanup_result.error().code);
        }

        Logger::Debug() << "Upload task cancelled: upload_id=" << upload_id;
        co_return {};
    }

    /// ==================== GetFileList ====================

    auto FileService::GetFileList(FileListRequest request, uint64_t user_id)
        -> drogon::Task<Result<FileListResponse>> {

        Logger::Debug() << "Starting get file list: parent_id=" << request.parent_id
                  << ", page=" << request.page << ", page_size=" << request.page_size
                  << ", sort_by=" << request.sort_by << ", sort_order=" << request.sort_order
                  << ", type=" << request.type << ", user_id=" << user_id;

        /// 1. 验证 parent_id 文件夹存在且属于用户（如果 parent_id != 0）
        if (request.parent_id != 0) {
            try {
                CoroMapper<Folders> folder_mapper(m_db_client);
                auto folder = co_await folder_mapper.findOne(
                    Criteria(Folders::Cols::_id, CompareOperator::EQ, request.parent_id) &&
                    Criteria(Folders::Cols::_user_id, CompareOperator::EQ, user_id)
                );
                Logger::Debug() << "Folder verification passed: folder_id=" << request.parent_id;
            } catch (const drogon::orm::DrogonDbException& e) {
                Logger::Warn() << "Folder not found or no permission: folder_id=" << request.parent_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
            }
        }

        /// 2. 使用 SQL 查询（JOIN 消除 N+1， LIMIT/OFFSET 宻除内存分页）
        std::vector<FileListItem> items;
        int total = 0;
        int total_pages = 0;

        const auto order_column = ResolveListSortColumn(request.sort_by, request.type == "folder");
        const std::string order_dir = (request.sort_order == "desc") ? "DESC" : "ASC";
        const std::string inner_order_by =
            BuildDeterministicOrderByClause(order_column, order_dir, request.type == "all");
        const std::string outer_order_by = BuildDeterministicOrderByClause(
            order_column,
            order_dir,
            request.type == "all",
            "page."
        );

        auto offset = (request.page - 1) * request.page_size;

        try {
            if (request.type == "all") {
                auto file_count_result = co_await m_db_client->execSqlCoro(
                    "SELECT COUNT(*) AS cnt FROM files f WHERE f.folder_id = $1 AND f.user_id = $2",
                    request.parent_id,
                    user_id
                );

                if (!file_count_result.empty()) {
                    total += static_cast<int>(file_count_result[0]["cnt"].as<int64_t>());
                }

                auto folder_count_result = co_await m_db_client->execSqlCoro(
                    "SELECT COUNT(*) AS cnt FROM folders fo WHERE fo.parent_id = $1 AND fo.user_id = $2",
                    request.parent_id,
                    user_id
                );

                if (!folder_count_result.empty()) {
                    total += static_cast<int>(folder_count_result[0]["cnt"].as<int64_t>());
                }

                /// 先在窄行结果集上完成分页，再回表补齐详情，避免在宽行 UNION 结果上提前排序。
                const std::string data_sql =
                    "SELECT page.id, page.name, page.type, page.size, " "       COALESCE(f.mime_type, '') AS mime_type, " "       COALESCE(fc.hash_md5, '') AS hash, " "       COALESCE(fo.item_count, 0) AS item_count, " "       page.created_at, page.updated_at " "FROM (" "  SELECT combined.id, combined.name, combined.type, combined.size, combined.created_at, combined.updated_at " "  FROM (" "    SELECT f.id, f.name, 'file' AS type, f.size, f.created_at, f.updated_at " "    FROM files f " "    WHERE f.folder_id = $1 AND f.user_id = $2 " "    UNION ALL " "    SELECT fo.id, fo.name, 'folder' AS type, 0 AS size, fo.created_at, fo.updated_at " "    FROM folders fo " "    WHERE fo.parent_id = $3 AND fo.user_id = $4 " "  ) AS combined " "  ORDER BY " + inner_order_by + " " "  LIMIT $5 OFFSET $6" ") AS page " "LEFT JOIN files f ON page.type = 'file' AND page.id = f.id " "LEFT JOIN folders fo ON page.type = 'folder' AND page.id = fo.id " "LEFT JOIN file_contents fc ON f.content_id = fc.id " "ORDER BY " + outer_order_by;

                auto paginated_result = co_await m_db_client->execSqlCoro(
                    data_sql,
                    request.parent_id,
                    user_id,
                    request.parent_id,
                    user_id,
                    request.page_size,
                    offset
                );

                for (const auto& row : paginated_result) {
                    FileListItem item;
                    item.id = row["id"].as<uint64_t>();
                    item.name = row["name"].as<std::string>();
                    item.type = row["type"].as<std::string>();
                    item.size = row["size"].as<uint64_t>();
                    item.mime_type = row["mime_type"].as<std::string>();
                    item.hash = row["hash"].as<std::string>();
                    item.item_count = static_cast<int>(row["item_count"].as<int64_t>());
                    item.created_at = row["created_at"].as<std::string>();
                    item.updated_at = row["updated_at"].as<std::string>();
                    items.push_back(item);
                }

            } else if (request.type == "file") {
                auto count_result = co_await m_db_client->execSqlCoro(
                    "SELECT COUNT(*) AS cnt FROM files WHERE folder_id = $1 AND user_id = $2",
                    request.parent_id,
                    user_id
                );

                if (!count_result.empty()) {
                    total = static_cast<int>(count_result[0]["cnt"].as<int64_t>());
                }

                const std::string data_sql =
                    "SELECT f.id, f.name, f.size, f.mime_type, " "       COALESCE(fc.hash_md5, '') AS hash, f.created_at, f.updated_at " "FROM (" "  SELECT f.id, f.name, f.size, f.created_at, f.updated_at " "  FROM files f " "  WHERE f.folder_id = $1 AND f.user_id = $2 " "  ORDER BY " + inner_order_by + " " "  LIMIT $3 OFFSET $4" ") AS page " "JOIN files f ON f.id = page.id " "LEFT JOIN file_contents fc ON f.content_id = fc.id " "ORDER BY " + outer_order_by;

                auto paginated_result = co_await m_db_client->execSqlCoro(
                    data_sql,
                    request.parent_id,
                    user_id,
                    request.page_size,
                    offset
                );

                for (const auto& row : paginated_result) {
                    FileListItem item;
                    item.id = row["id"].as<uint64_t>();
                    item.name = row["name"].as<std::string>();
                    item.type = "file";
                    item.size = row["size"].as<uint64_t>();
                    item.mime_type = row["mime_type"].as<std::string>();
                    item.hash = row["hash"].as<std::string>();
                    item.item_count = 0;
                    item.created_at = row["created_at"].as<std::string>();
                    item.updated_at = row["updated_at"].as<std::string>();
                    items.push_back(item);
                }

            } else if (request.type == "folder") {
                auto count_result = co_await m_db_client->execSqlCoro(
                    "SELECT COUNT(*) AS cnt FROM folders WHERE parent_id = $1 AND user_id = $2",
                    request.parent_id,
                    user_id
                );

                if (!count_result.empty()) {
                    total = static_cast<int>(count_result[0]["cnt"].as<int64_t>());
                }

                const std::string data_sql =
                    "SELECT page.id, page.name, page.item_count, page.created_at, page.updated_at " "FROM (" "  SELECT fo.id, fo.name, fo.item_count, fo.created_at, fo.updated_at, 0 AS sort_size " "  FROM folders fo " "  WHERE fo.parent_id = $1 AND fo.user_id = $2 " "  ORDER BY " + inner_order_by + " " "  LIMIT $3 OFFSET $4" ") AS page " "ORDER BY " + outer_order_by;

                auto paginated_result = co_await m_db_client->execSqlCoro(
                    data_sql,
                    request.parent_id,
                    user_id,
                    request.page_size,
                    offset
                );

                for (const auto& row : paginated_result) {
                    FileListItem item;
                    item.id = row["id"].as<uint64_t>();
                    item.name = row["name"].as<std::string>();
                    item.type = "folder";
                    item.size = 0;
                    item.mime_type = "";
                    item.hash = "";
                    item.item_count = static_cast<int>(row["item_count"].as<int64_t>());
                    item.created_at = row["created_at"].as<std::string>();
                    item.updated_at = row["updated_at"].as<std::string>();
                    items.push_back(item);
                }
            }

            total_pages = request.page_size > 0 ? (total + request.page_size - 1) / request.page_size : 0;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "Failed to query file list: " << e.base().what();
        }

        /// 3. 构造响应
        FileListResponse response;
        response.items = items;
        response.pagination = { .page = request.page,
                                .page_size = request.page_size,
                                .total = total,
                                .total_pages = total_pages };

        Logger::Debug() << "File list retrieved successfully: total=" << total
                  << ", page=" << request.page;
        co_return response;
    }

    /// ==================== GetFileDetail ====================

    auto FileService::GetFileDetail(uint64_t file_id, uint64_t user_id)
        -> drogon::Task<Result<FileDetailResponse>> {

        Logger::Debug() << "Starting get file detail: file_id=" << file_id << ", user_id=" << user_id;

        try {
            CoroMapper<Files> file_mapper(m_db_client);
            auto file = co_await file_mapper.findOne(
                Criteria(Files::Cols::_id, CompareOperator::EQ, file_id) &&
                Criteria(Files::Cols::_user_id, CompareOperator::EQ, user_id)
            );

            std::string hash;
            std::string mime_type = file.getValueOfMimeType();

            if (file.getContentId()) {
                CoroMapper<FileContents> content_mapper(m_db_client);
                auto content = co_await content_mapper.findOne(
                    Criteria(FileContents::Cols::_id, CompareOperator::EQ, *file.getContentId())
                );
                hash = content.getValueOfHashMd5();
                if (mime_type.empty()) {
                    mime_type = content.getValueOfMimeType();
                }
            }

            FileDetailResponse response;
            response.id = file.getValueOfId();
            response.name = file.getValueOfName();
            response.type = "file";
            response.size = file.getValueOfSize();
            response.hash = hash;
            response.mime_type = mime_type;
            response.parent_id = file.getValueOfFolderId();
            response.path = file.getValueOfPath();
            response.created_at = file.getValueOfCreatedAt().toDbStringLocal();
            response.updated_at = file.getValueOfUpdatedAt().toDbStringLocal();

            Logger::Debug() << "File detail retrieved successfully: name=" << response.name;
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "File not found or no permission: file_id=" << file_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::FileNotFound));
        }
    }

    /// ==================== GetDownloadInfo ====================

    auto FileService::GetDownloadInfo(uint64_t file_id, uint64_t user_id)
        -> drogon::Task<Result<DownloadInfoResponse>> {

        Logger::Debug() << "Starting get download info: file_id=" << file_id << ", user_id=" << user_id;

        /// 1. 查找文件并验证归属
        try {
            CoroMapper<Files> file_mapper(m_db_client);
            auto file = co_await file_mapper.findOne(
                Criteria(Files::Cols::_id, CompareOperator::EQ, file_id) &&
                Criteria(Files::Cols::_user_id, CompareOperator::EQ, user_id)
            );

            /// 2. 获取文件内容信息
            if (!file.getContentId()) {
                Logger::Error() << "File missing content_id: file_id=" << file_id;
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::FileReadError, "File content info missing")
                );
            }

            CoroMapper<FileContents> content_mapper(m_db_client);
            auto content = co_await content_mapper.findOne(
                Criteria(FileContents::Cols::_id, CompareOperator::EQ, *file.getContentId())
            );

            /// 3. 构造响应
            DownloadInfoResponse response;
            response.file_id = file.getValueOfId();
            response.filename = file.getValueOfName();
            response.file_size = file.getValueOfSize();
            response.file_hash = content.getValueOfHashMd5();
            response.mime_type = file.getValueOfMimeType().empty() ? content.getValueOfMimeType() :
                                                                     file.getValueOfMimeType();
            response.supports_range = true;

            Logger::Debug() << "Download info retrieved successfully: filename=" << response.filename
                      << ", size=" << response.file_size;
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "File not found or no permission: file_id=" << file_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::FileNotFound));
        }
    }

    /// ==================== GetDownloadData ====================

    auto FileService::GetDownloadData(uint64_t file_id, uint64_t user_id)
        -> drogon::Task<Result<DownloadInfo>> {

        Logger::Debug() << "Starting get download data: file_id=" << file_id << ", user_id=" << user_id;

        /// 1. 查找文件并验证归属
        try {
            CoroMapper<Files> file_mapper(m_db_client);
            auto file = co_await file_mapper.findOne(
                Criteria(Files::Cols::_id, CompareOperator::EQ, file_id) &&
                Criteria(Files::Cols::_user_id, CompareOperator::EQ, user_id)
            );

            /// 2. 获取文件内容信息
            if (!file.getContentId()) {
                Logger::Error() << "File missing content_id: file_id=" << file_id;
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::FileReadError, "File content info missing")
                );
            }

            CoroMapper<FileContents> content_mapper(m_db_client);
            auto content = co_await content_mapper.findOne(
                Criteria(FileContents::Cols::_id, CompareOperator::EQ, *file.getContentId())
            );

            /// 3. 构造响应
            DownloadInfo info;
            info.file_id = file.getValueOfId();
            info.filename = file.getValueOfName();
            info.file_size = file.getValueOfSize();
            info.file_hash = content.getValueOfHashMd5();
            info.mime_type = file.getValueOfMimeType().empty() ? content.getValueOfMimeType() :
                                                                 file.getValueOfMimeType();
            info.storage_path = content.getValueOfStoragePath();
            info.supports_range = true;

            Logger::Debug() << "Download data retrieved successfully: filename=" << info.filename
                      << ", storage_path=" << info.storage_path;
            co_return info;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "File not found or no permission: file_id=" << file_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::FileNotFound));
        }
    }

    /// ==================== Rename ====================

    auto FileService::Rename(uint64_t file_id, std::string new_name, uint64_t user_id)
        -> drogon::Task<Result<RenameResponse>> {

        Logger::Debug() << "Starting rename file: file_id=" << file_id << ", new_name=\"" << new_name
                  << "\""
                  << ", user_id=" << user_id;

        try {
            CoroMapper<Files> mapper(m_db_client);
            auto file = co_await mapper.findOne(
                Criteria(Files::Cols::_id, CompareOperator::EQ, file_id) &&
                Criteria(Files::Cols::_user_id, CompareOperator::EQ, user_id)
            );

            auto folder_id = file.getValueOfFolderId();
            if (file.getValueOfName() != new_name && co_await IsFilenameExists(folder_id, new_name, user_id)) {
                Logger::Warn() << "Target folder already has file with same name: " << new_name;
                co_return std::unexpected(ErrorInfo(ErrorCode::FileAlreadyExists));
            }

            auto folder_location_result = co_await ResolveFolderLocation(m_db_client, folder_id, user_id);
            if (!folder_location_result) {
                co_return std::unexpected(folder_location_result.error());
            }

            auto updated_at = trantor::Date::now();
            auto new_path = BuildFilePath(folder_location_result->path, new_name);
            auto update_result = co_await m_db_client->execSqlCoro(
                "UPDATE files SET name = $1, extension = $2, path = $3, updated_at = $4 "
                "WHERE id = $5 AND user_id = $6",
                new_name,
                ExtractExtension(new_name),
                new_path,
                updated_at,
                file_id,
                user_id
            );
            if (update_result.affectedRows() == 0) {
                co_return std::unexpected(ErrorInfo(ErrorCode::FileNotFound));
            }

            Logger::Info() << "File rename successful: file_id=" << file_id << ", new_name=\"" << new_name
                     << "\"";

            RenameResponse response;
            response.id = file.getValueOfId();
            response.name = new_name;
            response.updated_at = updated_at.toDbStringLocal();
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "File not found or no permission: file_id=" << file_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::FileNotFound));
        }
    }

    /// ==================== Move ====================

    auto FileService::Move(MoveRequest request, uint64_t user_id)
        -> drogon::Task<Result<MoveResponse>> {

        Logger::Debug() << "Starting move drive items: file_ids.size()=" << request.file_ids.size()
                  << ", folder_ids.size()=" << request.folder_ids.size()
                  << ", target_folder_id=" << request.target_folder_id << ", user_id=" << user_id;

        auto target_location_result =
            co_await ResolveFolderLocation(m_db_client, request.target_folder_id, user_id);
        if (!target_location_result) {
            co_return std::unexpected(target_location_result.error());
        }
        const auto target_location = *target_location_result;

        auto normalize_ids = [](std::vector<uint64_t> ids) {
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            return ids;
        };

        auto file_ids = normalize_ids(std::move(request.file_ids));
        auto folder_ids = normalize_ids(std::move(request.folder_ids));

        int moved_file_count = 0;
        int moved_folder_count = 0;

        std::shared_ptr<drogon::orm::Transaction> txn;
        try {
            txn = co_await m_db_client->newTransactionCoro();

            if (!file_ids.empty()) {
                auto chunks = BatchUtils::Chunk(file_ids, DEFAULT_BATCH_CHUNK_SIZE);
                for (const auto& chunk : chunks) {
                    if (chunk.empty()) {
                        continue;
                    }

                    auto result = co_await txn->execSqlCoro(
                        "SELECT id, folder_id, name FROM files WHERE id IN (" +
                            BatchUtils::BuildSafeNumericInClause(chunk) + ") AND user_id = $1",
                        user_id
                    );

                    std::unordered_map<uint64_t, std::pair<uint64_t, std::string>> files;
                    files.reserve(result.size());
                    std::vector<std::string> candidate_names;
                    candidate_names.reserve(result.size());
                    for (const auto& row : result) {
                        auto id = row["id"].as<uint64_t>();
                        auto folder_id = row["folder_id"].as<uint64_t>();
                        auto name = row["name"].as<std::string>();
                        files[id] = { folder_id, name };
                        if (folder_id != request.target_folder_id) {
                            candidate_names.push_back(name);
                        }
                    }

                    auto occupied_names = co_await QueryOccupiedNames(
                        txn,
                        request.target_folder_id,
                        user_id,
                        candidate_names
                    );

                    std::unordered_map<uint64_t, int> source_deltas;
                    for (const auto file_id : chunk) {
                        auto it = files.find(file_id);
                        if (it == files.end()) {
                            Logger::Warn() << "File not found or no permission, skipping move: file_id="
                                     << file_id;
                            continue;
                        }

                        auto source_folder_id = it->second.first;
                        const auto& name = it->second.second;
                        if (source_folder_id == request.target_folder_id) {
                            ++moved_file_count;
                            continue;
                        }

                        if (occupied_names.contains(name)) {
                            Logger::Warn() << "Target folder already has file with same name, skipping: "
                                     << name;
                            continue;
                        }
                        occupied_names.insert(name);

                        auto updated_at = trantor::Date::now();
                        auto update_result = co_await txn->execSqlCoro(
                            "UPDATE files SET folder_id = $1, path = $2, updated_at = $3 "
                            "WHERE id = $4 AND user_id = $5",
                            request.target_folder_id,
                            BuildFilePath(target_location.path, name),
                            updated_at,
                            file_id,
                            user_id
                        );
                        if (update_result.affectedRows() == 0) {
                            continue;
                        }

                        if (source_folder_id > 0) {
                            source_deltas[source_folder_id] -= 1;
                        }
                        if (request.target_folder_id > 0) {
                            source_deltas[request.target_folder_id] += 1;
                        }
                        ++moved_file_count;
                    }

                    for (const auto& [folder_id, delta] : source_deltas) {
                        if (delta == 0) {
                            continue;
                        }
                        co_await txn->execSqlCoro(
                            "UPDATE folders SET item_count = GREATEST(item_count + $1, 0), "
                            "updated_at = $2 WHERE id = $3 AND user_id = $4",
                            delta,
                            trantor::Date::now(),
                            folder_id,
                            user_id
                        );
                    }
                }
            }

            std::unordered_map<uint64_t, FolderDeletePlan> folder_plans =
                co_await FetchBatchFolderDeletePlans(txn, folder_ids, user_id);

            auto top_level_folder_ids = FilterCoveredFolderIds(folder_ids, folder_plans);
            std::vector<std::string> folder_candidate_names;
            folder_candidate_names.reserve(top_level_folder_ids.size());
            for (const auto folder_id : top_level_folder_ids) {
                auto plan_it = folder_plans.find(folder_id);
                if (plan_it == folder_plans.end()) {
                    continue;
                }
                if (plan_it->second.root.getValueOfParentId() != request.target_folder_id) {
                    folder_candidate_names.push_back(plan_it->second.root.getValueOfName());
                }
            }

            auto occupied_folder_names = co_await QueryOccupiedFolderNames(
                txn,
                request.target_folder_id,
                user_id,
                folder_candidate_names
            );

            for (const auto folder_id : top_level_folder_ids) {
                auto plan_it = folder_plans.find(folder_id);
                if (plan_it == folder_plans.end()) {
                    Logger::Warn() << "Folder not found or no permission, skipping move: folder_id="
                             << folder_id;
                    continue;
                }

                const auto& plan = plan_it->second;
                const auto& root = plan.root;
                auto old_parent_id = root.getValueOfParentId();
                const auto& folder_name = root.getValueOfName();

                auto moving_into_self_or_descendant = std::any_of(
                    plan.folders.begin(),
                    plan.folders.end(),
                    [&](const Folders& folder) {
                        return folder.getValueOfId() == request.target_folder_id;
                    }
                );
                if (moving_into_self_or_descendant) {
                    throw ServiceValidationException(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Cannot move a folder into itself or its descendant"
                    ));
                }

                if (old_parent_id == request.target_folder_id) {
                    ++moved_folder_count;
                    continue;
                }

                if (occupied_folder_names.contains(folder_name)) {
                    Logger::Warn() << "Target folder already has folder with same name, skipping: "
                             << folder_name;
                    continue;
                }
                occupied_folder_names.insert(folder_name);

                auto old_prefix = root.getValueOfPath();
                auto new_prefix = BuildFolderPath(target_location.path, folder_name);
                auto depth_delta = static_cast<int>(target_location.depth) + 1 -
                                   static_cast<int>(root.getValueOfDepth());

                for (const auto& folder : plan.folders) {
                    auto old_path = folder.getValueOfPath();
                    auto new_path = new_prefix + old_path.substr(old_prefix.size());
                    if (folder.getValueOfId() == root.getValueOfId()) {
                        co_await txn->execSqlCoro(
                            "UPDATE folders SET parent_id = $1, path = $2, depth = depth + $3, "
                            "updated_at = $4 WHERE id = $5 AND user_id = $6",
                            request.target_folder_id,
                            new_path,
                            depth_delta,
                            trantor::Date::now(),
                            folder.getValueOfId(),
                            user_id
                        );
                    } else {
                        co_await txn->execSqlCoro(
                            "UPDATE folders SET path = $1, depth = depth + $2, updated_at = $3 "
                            "WHERE id = $4 AND user_id = $5",
                            new_path,
                            depth_delta,
                            trantor::Date::now(),
                            folder.getValueOfId(),
                            user_id
                        );
                    }
                }

                std::unordered_map<uint64_t, std::string> folder_paths;
                folder_paths.reserve(plan.folders.size());
                for (const auto& folder : plan.folders) {
                    auto old_path = folder.getValueOfPath();
                    folder_paths[folder.getValueOfId()] = new_prefix + old_path.substr(old_prefix.size());
                }
                for (const auto& file : plan.files) {
                    auto path_it = folder_paths.find(file.getValueOfFolderId());
                    if (path_it == folder_paths.end()) {
                        continue;
                    }
                    co_await txn->execSqlCoro(
                        "UPDATE files SET path = $1, updated_at = $2 WHERE id = $3 AND user_id = $4",
                        BuildFilePath(path_it->second, file.getValueOfName()),
                        trantor::Date::now(),
                        file.getValueOfId(),
                        user_id
                    );
                }

                if (old_parent_id > 0) {
                    co_await txn->execSqlCoro(
                        "UPDATE folders SET item_count = GREATEST(item_count - 1, 0), "
                        "updated_at = $1 WHERE id = $2 AND user_id = $3",
                        trantor::Date::now(),
                        old_parent_id,
                        user_id
                    );
                }
                if (request.target_folder_id > 0) {
                    co_await txn->execSqlCoro(
                        "UPDATE folders SET item_count = item_count + 1, updated_at = $1 "
                        "WHERE id = $2 AND user_id = $3",
                        trantor::Date::now(),
                        request.target_folder_id,
                        user_id
                    );
                }

                ++moved_folder_count;
            }
        } catch (const ServiceValidationException& e) {
            if (txn) {
                try {
                    txn->rollback();
                } catch (const std::exception& rb_e) {
                    Logger::Error() << "Transaction rollback failed: " << rb_e.what();
                }
            }
            co_return std::unexpected(e.error);
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Move transaction failed (DB): " << e.base().what();
            if (txn) {
                try {
                    txn->rollback();
                } catch (const std::exception& rb_e) {
                    Logger::Error() << "Transaction rollback failed: " << rb_e.what();
                }
            }
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Failed to move items"));
        } catch (const std::exception& e) {
            Logger::Error() << "Move transaction failed: " << e.what();
            if (txn) {
                try {
                    txn->rollback();
                } catch (const std::exception& rb_e) {
                    Logger::Error() << "Transaction rollback failed: " << rb_e.what();
                }
            }
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Failed to move items"));
        }

        Logger::Info() << "Move completed: moved_file_count=" << moved_file_count
                 << ", moved_folder_count=" << moved_folder_count;

        MoveResponse response;
        response.moved_file_count = moved_file_count;
        response.moved_folder_count = moved_folder_count;
        response.moved_count = moved_file_count + moved_folder_count;
        co_return response;
    }

    /// ==================== Copy ====================

    auto FileService::Copy(CopyRequest request, uint64_t user_id)
        -> drogon::Task<Result<CopyResponse>> {

        Logger::Debug() << "Starting copy items: file_ids.size()=" << request.file_ids.size()
                  << ", folder_ids.size()=" << request.folder_ids.size()
                  << ", target_folder_id=" << request.target_folder_id << ", user_id=" << user_id;

        auto normalize_ids = [](std::vector<uint64_t> ids) {
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            return ids;
        };

        auto requested_file_ids = normalize_ids(std::move(request.file_ids));
        auto requested_folder_ids = normalize_ids(std::move(request.folder_ids));

        auto target_location_result = co_await ResolveFolderLocation(
            m_db_client,
            request.target_folder_id,
            user_id
        );
        if (!target_location_result) {
            Logger::Warn() << "Target folder not found or no permission: folder_id="
                     << request.target_folder_id;
            co_return std::unexpected(target_location_result.error());
        }

        std::unordered_map<uint64_t, FolderDeletePlan> folder_plans =
            co_await FetchBatchFolderDeletePlans(m_db_client, requested_folder_ids, user_id);

        auto top_level_folder_ids = FilterCoveredFolderIds(requested_folder_ids, folder_plans);
        auto covered_file_ids = CollectCoveredFileIds(top_level_folder_ids, folder_plans);

        std::vector<uint64_t> explicit_file_ids;
        explicit_file_ids.reserve(requested_file_ids.size());
        for (const auto file_id : requested_file_ids) {
            if (covered_file_ids.contains(file_id)) {
                Logger::Debug() << "Skipping explicit file copy covered by folder copy: file_id=" << file_id;
                continue;
            }
            explicit_file_ids.push_back(file_id);
        }

        uint64_t total_copy_size = 0;
        std::vector<std::pair<uint64_t, Files>> files_to_copy;

        auto id_chunks = BatchUtils::Chunk(explicit_file_ids, DEFAULT_BATCH_CHUNK_SIZE);
        for (const auto& chunk : id_chunks) {
            if (chunk.empty()) {
                continue;
            }

            std::unordered_map<uint64_t, Files> file_map;
            file_map.reserve(chunk.size());

            try {
                auto result = co_await m_db_client->execSqlCoro(
                    "SELECT id, user_id, folder_id, content_id, name, extension, size, mime_type, path, "
                    "is_favorite, download_count, last_accessed_at, created_at, updated_at "
                    "FROM files WHERE id IN (" + BatchUtils::BuildSafeNumericInClause(chunk) + ") AND user_id = $1",
                    user_id
                );

                for (const auto& row : result) {
                    auto file = Files(row, -1);
                    file_map[file.getValueOfId()] = std::move(file);
                }
            } catch (const drogon::orm::DrogonDbException& e) {
                Logger::Warn() << "File batch fetch failed in copy, skipping chunk: " << e.base().what();
                continue;
            }

            for (const auto file_id : chunk) {
                auto it = file_map.find(file_id);
                if (it == file_map.end()) {
                    Logger::Warn() << "File not found or no permission, skipping: file_id=" << file_id;
                    continue;
                }
                total_copy_size += it->second.getValueOfSize();
                files_to_copy.emplace_back(file_id, it->second);
            }
        }

        for (const auto folder_id : top_level_folder_ids) {
            auto plan_it = folder_plans.find(folder_id);
            if (plan_it == folder_plans.end()) {
                continue;
            }
            total_copy_size += plan_it->second.item_size;
        }

        if (total_copy_size > 0) {
            auto quota_result = co_await CheckStorageQuota(m_db_client, user_id, total_copy_size);
            if (!quota_result) {
                Logger::Warn() << "Storage quota check failed for copy: user_id=" << user_id
                         << ", total_copy_size=" << total_copy_size;
                co_return std::unexpected(quota_result.error());
            }
        }

        int copied_file_count = 0;
        int copied_folder_count = 0;
        uint64_t actual_copy_size = 0;
        std::vector<FileIdMapping> new_files;
        std::vector<FileIdMapping> new_folders;

        auto copy_chunks = BatchUtils::Chunk(files_to_copy, DEFAULT_BATCH_CHUNK_SIZE);
        for (const auto& chunk : copy_chunks) {
            if (chunk.empty()) {
                continue;
            }

            std::vector<std::string> candidate_names;
            candidate_names.reserve(chunk.size());
            for (const auto& [old_id, file] : chunk) {
                candidate_names.push_back(file.getValueOfName());
            }

            std::unordered_set<std::string> occupied_names;
            try {
                occupied_names = co_await QueryOccupiedNames(
                    m_db_client,
                    request.target_folder_id,
                    user_id,
                    candidate_names
                );
            } catch (const drogon::orm::DrogonDbException& e) {
                Logger::Warn() << "Filename conflict query failed in copy, skipping chunk: "
                         << e.base().what();
                continue;
            }

            struct PendingCopyItem {
                uint64_t old_id;
                Files file;
            };

            std::vector<PendingCopyItem> pending_items;
            pending_items.reserve(chunk.size());
            std::unordered_map<uint64_t, uint64_t> content_ref_increment;

            for (const auto& [old_id, file] : chunk) {
                if (occupied_names.contains(file.getValueOfName())) {
                    Logger::Warn() << "Target folder already has file with same name, skipping: "
                             << file.getValueOfName();
                    continue;
                }

                occupied_names.insert(file.getValueOfName());
                if (file.getContentId()) {
                    content_ref_increment[*file.getContentId()] += 1;
                }
                pending_items.push_back({ .old_id = old_id, .file = file });
            }

            std::unordered_set<uint64_t> existing_content_ids;
            if (!content_ref_increment.empty()) {
                std::vector<uint64_t> content_ids;
                content_ids.reserve(content_ref_increment.size());
                for (const auto& [content_id, _] : content_ref_increment) {
                    content_ids.push_back(content_id);
                }

                try {
                    auto content_result = co_await m_db_client->execSqlCoro(
                        "SELECT id FROM file_contents WHERE id IN (" +
                        BatchUtils::BuildSafeNumericInClause(content_ids) + ")"
                    );
                    for (const auto& row : content_result) {
                        existing_content_ids.insert(row["id"].as<uint64_t>());
                    }
                } catch (const drogon::orm::DrogonDbException& e) {
                    Logger::Warn() << "File content batch query failed in copy, skipping chunk: "
                             << e.base().what();
                    continue;
                }
            }

            std::vector<std::pair<uint64_t, const Files*>> valid_items;
            valid_items.reserve(pending_items.size());
            for (const auto& pending : pending_items) {
                auto content_id_ptr = pending.file.getContentId();
                if (content_id_ptr && !existing_content_ids.contains(*content_id_ptr)) {
                    Logger::Warn() << "File content not found during copy: content_id=" << *content_id_ptr;
                    continue;
                }
                valid_items.emplace_back(pending.old_id, &pending.file);
            }

            if (valid_items.empty()) {
                continue;
            }

            std::unordered_map<uint64_t, uint64_t> old_id_to_size;
            for (const auto& [old_id, file_ptr] : valid_items) {
                old_id_to_size[old_id] = file_ptr->getValueOfSize();
            }

            std::shared_ptr<drogon::orm::Transaction> txn;
            try {
                txn = co_await m_db_client->newTransactionCoro();

                auto incremented_ids = co_await IncrementContentRefCount(
                    txn,
                    content_ref_increment,
                    existing_content_ids
                );

                std::vector<std::pair<uint64_t, const Files*>> txn_valid_items;
                txn_valid_items.reserve(valid_items.size());
                for (const auto& [old_id, file_ptr] : valid_items) {
                    auto cid = file_ptr->getContentId();
                    if (cid && !incremented_ids.contains(*cid)) {
                        Logger::Warn() << "Content ref_count increment skipped in txn, dropping file: content_id="
                                 << *cid;
                        continue;
                    }
                    txn_valid_items.emplace_back(old_id, file_ptr);
                }

                auto id_mappings = co_await InsertCopiedFiles(
                    txn,
                    user_id,
                    request.target_folder_id,
                    txn_valid_items
                );

                for (const auto& [old_id, new_id] : id_mappings) {
                    ++copied_file_count;
                    actual_copy_size += old_id_to_size[old_id];
                    new_files.push_back({ .old_id = old_id, .new_id = new_id });
                }
            } catch (const std::exception& e) {
                Logger::Error() << "Copy file batch transaction failed: " << e.what();
                if (txn) {
                    try {
                        txn->rollback();
                    } catch (const std::exception& rb_e) {
                        Logger::Error() << "Transaction rollback failed: " << rb_e.what();
                    }
                }
            }
        }

        std::vector<std::string> root_folder_names;
        root_folder_names.reserve(top_level_folder_ids.size());
        for (const auto folder_id : top_level_folder_ids) {
            auto plan_it = folder_plans.find(folder_id);
            if (plan_it != folder_plans.end()) {
                root_folder_names.push_back(plan_it->second.root.getValueOfName());
            }
        }

        std::unordered_set<std::string> occupied_root_folder_names;
        try {
            occupied_root_folder_names = co_await QueryOccupiedFolderNames(
                m_db_client,
                request.target_folder_id,
                user_id,
                root_folder_names
            );
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "Folder conflict query failed in copy: " << e.base().what();
            occupied_root_folder_names.clear();
        }

        for (const auto folder_id : top_level_folder_ids) {
            auto plan_it = folder_plans.find(folder_id);
            if (plan_it == folder_plans.end()) {
                continue;
            }

            const auto& plan = plan_it->second;
            auto target_inside_source = std::any_of(
                plan.folders.begin(),
                plan.folders.end(),
                [target_folder_id = request.target_folder_id](const Folders& folder) {
                    return folder.getValueOfId() == target_folder_id;
                }
            );
            if (target_inside_source) {
                Logger::Warn() << "Cannot copy folder into itself or descendant, skipping: folder_id="
                         << folder_id;
                continue;
            }

            auto root_name = plan.root.getValueOfName();
            if (occupied_root_folder_names.contains(root_name)) {
                Logger::Warn() << "Target folder already has folder with same name, skipping: " << root_name;
                continue;
            }
            occupied_root_folder_names.insert(root_name);

            std::unordered_map<uint64_t, uint64_t> content_ref_increment;
            for (const auto& file : plan.files) {
                if (file.getContentId()) {
                    content_ref_increment[*file.getContentId()] += 1;
                }
            }

            std::unordered_set<uint64_t> existing_content_ids;
            if (!content_ref_increment.empty()) {
                std::vector<uint64_t> content_ids;
                content_ids.reserve(content_ref_increment.size());
                for (const auto& [content_id, _] : content_ref_increment) {
                    content_ids.push_back(content_id);
                }

                try {
                    auto content_result = co_await m_db_client->execSqlCoro(
                        "SELECT id FROM file_contents WHERE id IN (" +
                        BatchUtils::BuildSafeNumericInClause(content_ids) + ")"
                    );
                    for (const auto& row : content_result) {
                        existing_content_ids.insert(row["id"].as<uint64_t>());
                    }
                } catch (const drogon::orm::DrogonDbException& e) {
                    Logger::Warn() << "Folder copy content query failed, skipping folder_id=" << folder_id
                             << ": " << e.base().what();
                    continue;
                }
            }

            std::shared_ptr<drogon::orm::Transaction> txn;
            try {
                txn = co_await m_db_client->newTransactionCoro();

                auto incremented_ids = co_await IncrementContentRefCount(
                    txn,
                    content_ref_increment,
                    existing_content_ids
                );

                CoroMapper<Folders> folder_mapper(txn);
                CoroMapper<Files> file_mapper(txn);

                std::unordered_map<uint64_t, uint64_t> folder_id_map;
                std::unordered_map<uint64_t, std::string> folder_path_map;
                folder_id_map.reserve(plan.folders.size());
                folder_path_map.reserve(plan.folders.size());

                std::vector<FileIdMapping> folder_mappings;
                std::vector<FileIdMapping> file_mappings;
                uint64_t copied_size = 0;
                int folder_count = 0;
                int file_count = 0;

                auto root_path = BuildFolderPath(target_location_result->path, root_name);
                auto root_depth = target_location_result->depth + 1;

                Folders root_folder;
                root_folder.setUserId(user_id);
                root_folder.setParentId(request.target_folder_id);
                root_folder.setName(root_name);
                root_folder.setPath(root_path);
                root_folder.setDepth(root_depth);
                root_folder.setItemCount(plan.root.getValueOfItemCount());
                root_folder.setCreatedAt(trantor::Date::now());
                root_folder.setUpdatedAt(trantor::Date::now());

                auto inserted_root = co_await folder_mapper.insert(root_folder);
                folder_id_map[plan.root.getValueOfId()] = inserted_root.getValueOfId();
                folder_path_map[plan.root.getValueOfId()] = root_path;
                folder_mappings.push_back({ .old_id = plan.root.getValueOfId(),
                                            .new_id = inserted_root.getValueOfId() });
                ++folder_count;

                for (const auto& folder : plan.folders) {
                    if (folder.getValueOfId() == plan.root.getValueOfId()) {
                        continue;
                    }

                    auto parent_it = folder_id_map.find(folder.getValueOfParentId());
                    auto parent_path_it = folder_path_map.find(folder.getValueOfParentId());
                    if (parent_it == folder_id_map.end() || parent_path_it == folder_path_map.end()) {
                        throw std::runtime_error("Folder copy plan contains orphaned folder node");
                    }

                    auto folder_path = BuildFolderPath(parent_path_it->second, folder.getValueOfName());
                    auto depth_delta = folder.getValueOfDepth() > plan.root.getValueOfDepth()
                        ? folder.getValueOfDepth() - plan.root.getValueOfDepth()
                        : 1;

                    Folders copied_folder;
                    copied_folder.setUserId(user_id);
                    copied_folder.setParentId(parent_it->second);
                    copied_folder.setName(folder.getValueOfName());
                    copied_folder.setPath(folder_path);
                    copied_folder.setDepth(root_depth + depth_delta);
                    copied_folder.setItemCount(folder.getValueOfItemCount());
                    copied_folder.setCreatedAt(trantor::Date::now());
                    copied_folder.setUpdatedAt(trantor::Date::now());

                    auto inserted_folder = co_await folder_mapper.insert(copied_folder);
                    folder_id_map[folder.getValueOfId()] = inserted_folder.getValueOfId();
                    folder_path_map[folder.getValueOfId()] = folder_path;
                    folder_mappings.push_back({ .old_id = folder.getValueOfId(),
                                                .new_id = inserted_folder.getValueOfId() });
                    ++folder_count;
                }

                for (const auto& file : plan.files) {
                    auto folder_it = folder_id_map.find(file.getValueOfFolderId());
                    auto path_it = folder_path_map.find(file.getValueOfFolderId());
                    if (folder_it == folder_id_map.end() || path_it == folder_path_map.end()) {
                        throw std::runtime_error("Folder copy plan contains orphaned file node");
                    }

                    auto content_id_ptr = file.getContentId();
                    if (content_id_ptr && !incremented_ids.contains(*content_id_ptr)) {
                        Logger::Warn() << "Content ref_count increment skipped in folder copy, dropping file: content_id="
                                 << *content_id_ptr;
                        continue;
                    }

                    Files copied_file;
                    copied_file.setUserId(user_id);
                    if (content_id_ptr) {
                        copied_file.setContentId(*content_id_ptr);
                    }
                    copied_file.setFolderId(folder_it->second);
                    copied_file.setName(file.getValueOfName());
                    copied_file.setExtension(file.getValueOfExtension());
                    copied_file.setSize(file.getValueOfSize());
                    copied_file.setMimeType(file.getValueOfMimeType());
                    copied_file.setPath(BuildFilePath(path_it->second, file.getValueOfName()));
                    copied_file.setIsFavorite(0);
                    copied_file.setDownloadCount(0);
                    copied_file.setCreatedAt(trantor::Date::now());
                    copied_file.setUpdatedAt(trantor::Date::now());

                    auto inserted_file = co_await file_mapper.insert(copied_file);
                    file_mappings.push_back({ .old_id = file.getValueOfId(),
                                              .new_id = inserted_file.getValueOfId() });
                    ++file_count;
                    copied_size += file.getValueOfSize();
                }

                if (request.target_folder_id > 0) {
                    co_await txn->execSqlCoro(
                        "UPDATE folders SET item_count = item_count + 1, updated_at = $1 "
                        "WHERE id = $2 AND user_id = $3",
                        trantor::Date::now(),
                        request.target_folder_id,
                        user_id
                    );
                }

                copied_folder_count += folder_count;
                copied_file_count += file_count;
                actual_copy_size += copied_size;
                new_folders.insert(new_folders.end(), folder_mappings.begin(), folder_mappings.end());
                new_files.insert(new_files.end(), file_mappings.begin(), file_mappings.end());
            } catch (const std::exception& e) {
                Logger::Error() << "Folder copy transaction failed: folder_id=" << folder_id
                          << ", error=" << e.what();
                if (txn) {
                    try {
                        txn->rollback();
                    } catch (const std::exception& rb_e) {
                        Logger::Error() << "Transaction rollback failed: " << rb_e.what();
                    }
                }
            }
        }

        auto reserved_size = static_cast<int64_t>(total_copy_size);
        auto consumed_size = static_cast<int64_t>(actual_copy_size);
        auto release_size = reserved_size - consumed_size;
        if (release_size > 0) {
            co_await UpdateStorageUsed(m_db_client, user_id, -release_size);
        }

        CopyResponse response;
        response.copied_file_count = copied_file_count;
        response.copied_folder_count = copied_folder_count;
        response.copied_count = copied_file_count + copied_folder_count;
        response.new_files = std::move(new_files);
        response.new_folders = std::move(new_folders);

        Logger::Info() << "Copy completed: copied_files=" << response.copied_file_count
                 << ", copied_folders=" << response.copied_folder_count
                 << ", total_size=" << actual_copy_size;

        co_return response;
    }

    /// ==================== Delete ====================

    auto FileService::Delete(DeleteRequest request, uint64_t user_id)
        -> drogon::Task<Result<DeleteResponse>> {

        Logger::Debug() << "Starting delete items: file_ids.size()=" << request.file_ids.size()
                  << ", folder_ids.size()=" << request.folder_ids.size() << ", user_id=" << user_id;

        auto delete_start = std::chrono::steady_clock::now();

        auto normalize_ids = [](std::vector<uint64_t> ids) {
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            return ids;
        };

        auto requested_file_ids = normalize_ids(std::move(request.file_ids));
        auto requested_folder_ids = normalize_ids(std::move(request.folder_ids));

        std::unordered_map<uint64_t, FolderDeletePlan> folder_plans =
            co_await FetchBatchFolderDeletePlans(m_db_client, requested_folder_ids, user_id);

        auto top_level_folder_ids = FilterCoveredFolderIds(requested_folder_ids, folder_plans);
        auto covered_file_ids = CollectCoveredFileIds(top_level_folder_ids, folder_plans);

        std::vector<uint64_t> explicit_file_ids;
        explicit_file_ids.reserve(requested_file_ids.size());
        for (const auto file_id : requested_file_ids) {
            if (covered_file_ids.contains(file_id)) {
                Logger::Debug() << "Skipping explicit file delete covered by folder delete: file_id=" << file_id;
                continue;
            }
            explicit_file_ids.push_back(file_id);
        }

        std::unordered_map<uint64_t, Files> file_map;
        file_map.reserve(explicit_file_ids.size());
        auto file_chunks = BatchUtils::Chunk(explicit_file_ids, DEFAULT_BATCH_CHUNK_SIZE);
        for (const auto& chunk : file_chunks) {
            if (chunk.empty()) {
                continue;
            }

            try {
                auto result = co_await m_db_client->execSqlCoro(
                    "SELECT id, user_id, folder_id, content_id, name, extension, size, mime_type, path, "
                    "is_favorite, download_count, last_accessed_at, created_at, updated_at "
                    "FROM files WHERE id IN (" + BatchUtils::BuildSafeNumericInClause(chunk) + ") AND user_id = $1",
                    user_id
                );

                for (const auto& row : result) {
                    auto file = Files(row, -1);
                    file_map[file.getValueOfId()] = std::move(file);
                }
            } catch (const drogon::orm::DrogonDbException& e) {
                Logger::Warn() << "File batch fetch failed in delete, skipping chunk: " << e.base().what();
            }
        }

        std::vector<TrashInsertItem> trash_items;
        trash_items.reserve(file_map.size() + top_level_folder_ids.size());

        std::vector<uint64_t> file_ids_to_delete;
        file_ids_to_delete.reserve(file_map.size() + covered_file_ids.size());

        int deleted_file_count = 0;
        for (const auto file_id : explicit_file_ids) {
            auto it = file_map.find(file_id);
            if (it == file_map.end()) {
                Logger::Warn() << "File not found or delete failed, skipping: file_id=" << file_id;
                continue;
            }

            const auto& file = it->second;
            Json::Value item_data;
            if (file.getContentId()) {
                item_data["content_id"] = static_cast<Json::UInt64>(*file.getContentId());
            }
            item_data["mime_type"] = file.getValueOfMimeType();
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";

            trash_items.push_back({
                .item_type = "file",
                .item_id = file.getValueOfId(),
                .item_name = file.getValueOfName(),
                .item_size = file.getValueOfSize(),
                .original_folder_id = file.getValueOfFolderId(),
                .original_path = file.getValueOfPath(),
                .content_id = file.getContentId() ? std::optional<uint64_t>(*file.getContentId()) : std::nullopt,
                .item_data = Json::writeString(builder, item_data),
            });
            file_ids_to_delete.push_back(file.getValueOfId());
            ++deleted_file_count;
        }

        std::vector<uint64_t> folder_ids_to_delete;
        int deleted_folder_count = 0;
        for (const auto folder_id : top_level_folder_ids) {
            auto plan_it = folder_plans.find(folder_id);
            if (plan_it == folder_plans.end()) {
                continue;
            }

            const auto& plan = plan_it->second;
            trash_items.push_back({
                .item_type = "folder",
                .item_id = plan.root.getValueOfId(),
                .item_name = plan.root.getValueOfName(),
                .item_size = plan.item_size,
                .original_folder_id = plan.root.getValueOfParentId(),
                .original_path = plan.root.getValueOfPath(),
                .content_id = std::nullopt,
                .item_data = BuildFolderSnapshot(plan),
            });

            for (const auto& file : plan.files) {
                file_ids_to_delete.push_back(file.getValueOfId());
            }
            for (const auto& folder : plan.folders) {
                folder_ids_to_delete.push_back(folder.getValueOfId());
            }
            ++deleted_folder_count;
        }

        file_ids_to_delete = normalize_ids(std::move(file_ids_to_delete));
        folder_ids_to_delete = normalize_ids(std::move(folder_ids_to_delete));

        if (trash_items.empty()) {
            co_return std::unexpected(ErrorInfo(
                ErrorCode::FileNotFound,
                "No deletable files or folders found for the given IDs"
            ));
        }

        std::shared_ptr<drogon::orm::Transaction> txn;
        try {
            txn = co_await m_db_client->newTransactionCoro();

            auto insert_ok = co_await InsertTrashRecords(txn, trash_items, user_id);
            if (!insert_ok) {
                throw std::runtime_error("Failed to insert trash records");
            }

            std::vector<uint64_t> affected_share_ids;
            auto deleted_file_share_links = 0;
            auto file_share_chunks = BatchUtils::Chunk(file_ids_to_delete, DEFAULT_BATCH_CHUNK_SIZE);
            for (const auto& chunk : file_share_chunks) {
                if (chunk.empty()) {
                    continue;
                }
                auto linked_shares = co_await txn->execSqlCoro(
                    "SELECT DISTINCT share_id FROM share_files WHERE item_type = 'file' AND item_id IN (" +
                        BatchUtils::BuildSafeNumericInClause(chunk) + ")"
                );
                for (const auto& row : linked_shares) {
                    affected_share_ids.push_back(row["share_id"].as<uint64_t>());
                }
                auto result = co_await txn->execSqlCoro(
                    "DELETE FROM share_files WHERE item_type = 'file' AND item_id IN (" +
                        BatchUtils::BuildSafeNumericInClause(chunk) + ")"
                );
                deleted_file_share_links += static_cast<int>(result.affectedRows());
            }

            auto deleted_folder_share_links = 0;
            auto folder_share_chunks = BatchUtils::Chunk(folder_ids_to_delete, DEFAULT_BATCH_CHUNK_SIZE);
            for (const auto& chunk : folder_share_chunks) {
                if (chunk.empty()) {
                    continue;
                }
                auto linked_shares = co_await txn->execSqlCoro(
                    "SELECT DISTINCT share_id FROM share_files WHERE item_type = 'folder' AND item_id IN (" +
                        BatchUtils::BuildSafeNumericInClause(chunk) + ")"
                );
                for (const auto& row : linked_shares) {
                    affected_share_ids.push_back(row["share_id"].as<uint64_t>());
                }
                auto result = co_await txn->execSqlCoro(
                    "DELETE FROM share_files WHERE item_type = 'folder' AND item_id IN (" +
                        BatchUtils::BuildSafeNumericInClause(chunk) + ")"
                );
                deleted_folder_share_links += static_cast<int>(result.affectedRows());
            }

            affected_share_ids = normalize_ids(std::move(affected_share_ids));
            auto cancelled_empty_shares = 0;
            auto affected_share_chunks = BatchUtils::Chunk(affected_share_ids, DEFAULT_BATCH_CHUNK_SIZE);
            for (const auto& chunk : affected_share_chunks) {
                if (chunk.empty()) {
                    continue;
                }
                auto result = co_await txn->execSqlCoro(
                    "UPDATE shares s SET s.status = 0, s.updated_at = NOW() "
                    "WHERE s.status = 1 AND s.id IN (" + BatchUtils::BuildSafeNumericInClause(chunk) + ") "
                    "AND NOT EXISTS (SELECT 1 FROM share_files sf WHERE sf.share_id = s.id)"
                );
                cancelled_empty_shares += static_cast<int>(result.affectedRows());
            }

            Logger::Debug() << "Cleaned share links during delete: file_links=" << deleted_file_share_links
                      << ", folder_links=" << deleted_folder_share_links
                      << ", cancelled_empty_shares=" << cancelled_empty_shares;

            auto deleted_file_rows = co_await DeleteFilesByIds(txn, file_ids_to_delete);
            if (deleted_file_rows != static_cast<int>(file_ids_to_delete.size())) {
                throw std::runtime_error("Failed to delete all file rows");
            }

            auto deleted_folder_rows = co_await DeleteFoldersByIds(txn, folder_ids_to_delete);
            if (deleted_folder_rows != static_cast<int>(folder_ids_to_delete.size())) {
                throw std::runtime_error("Failed to delete all folder rows");
            }
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Delete transaction failed (DB): " << e.base().what();
            if (txn) {
                try {
                    txn->rollback();
                } catch (const std::exception& rb_e) {
                    Logger::Error() << "Transaction rollback failed: " << rb_e.what();
                }
            }
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Failed to delete items"));
        } catch (const std::exception& e) {
            Logger::Error() << "Delete transaction failed: " << e.what();
            if (txn) {
                try {
                    txn->rollback();
                } catch (const std::exception& rb_e) {
                    Logger::Error() << "Transaction rollback failed: " << rb_e.what();
                }
            }
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Failed to delete items"));
        }

        auto deleted_count = deleted_file_count + deleted_folder_count;
        auto delete_elapsed = std::chrono::steady_clock::now() - delete_start;
        Logger::Info() << "FileService::Delete completed: deleted_count=" << deleted_count
                 << ", deleted_file_count=" << deleted_file_count
                 << ", deleted_folder_count=" << deleted_folder_count
                 << ", removed_file_rows=" << file_ids_to_delete.size()
                 << ", removed_folder_rows=" << folder_ids_to_delete.size()
                 << ", elapsed_ms="
                 << std::chrono::duration_cast<std::chrono::milliseconds>(delete_elapsed).count();

        DeleteResponse response;
        response.deleted_count = deleted_count;
        response.deleted_file_count = deleted_file_count;
        response.deleted_folder_count = deleted_folder_count;
        co_return response;
    }

    /// ==================== Search ====================

    auto FileService::Search(SearchRequest request, uint64_t user_id)
        -> drogon::Task<Result<SearchResponse>> {

        Logger::Debug() << "Starting search file: keyword=\"" << request.keyword
                  << "\", type=" << request.type << ", folder_id="
                  << (request.folder_id.has_value() ? std::to_string(*request.folder_id) : "null")
                  << ", page=" << request.page << ", page_size=" << request.page_size
                  << ", user_id=" << user_id;

        std::vector<SearchResultItem> items;
        int total = 0;
        int total_pages = 0;

        const bool use_fulltext = IsFulltextEligible(request.keyword);
        const bool has_folder_filter = request.folder_id.has_value();
        const auto normalized_keyword = NormalizeFulltextKeyword(request.keyword);

        /// Escape underscore for LIKE pattern (treat it literally, not as wildcard)
        std::string escaped_like_keyword = request.keyword;
        size_t pos = 0;
        while ((pos = escaped_like_keyword.find('_', pos)) != std::string::npos) {
            escaped_like_keyword.replace(pos, 1, "\\_");
            pos += 2; ///< Skip past the escaped character
        }

        const std::string search_param =
            use_fulltext ? normalized_keyword : "%" + escaped_like_keyword + "%";
        const std::string inner_order_by =
            BuildDeterministicOrderByClause("name", "ASC", request.type == "all");
        const std::string outer_order_by = BuildDeterministicOrderByClause(
            "name",
            "ASC",
            request.type == "all",
            "page."
        );
        auto offset = (request.page - 1) * request.page_size;

        try {
            std::string file_where = use_fulltext ?
                                          "WHERE f.user_id = $1 AND to_tsvector('simple', f.name) @@ to_tsquery('simple', replace($2, ' ', ' | '))" :
                                          "WHERE f.user_id = $1 AND f.name LIKE $2";
            std::string folder_where = use_fulltext ?
                                            "WHERE fo.user_id = $1 AND to_tsvector('simple', fo.name) @@ to_tsquery('simple', replace($2, ' ', ' | '))" :
                                            "WHERE fo.user_id = $1 AND fo.name LIKE $2";

            if (has_folder_filter) {
                file_where += " AND f.folder_id = $3";
                folder_where += " AND fo.parent_id = $3";
            }

            if (request.type == "all") {
                const std::string file_count_sql =
                    "SELECT COUNT(*) AS cnt FROM files f " + file_where;
                const std::string folder_count_sql =
                    "SELECT COUNT(*) AS cnt FROM folders fo " + folder_where;
                const std::string data_sql =
                    "SELECT page.id, page.name, page.type, " "       COALESCE(f.size, 0) AS size, " "       COALESCE(f.mime_type, '') AS mime_type, " "       COALESCE(fc.hash_md5, '') AS hash, " "       COALESCE(fo.item_count, 0) AS item_count, " "       COALESCE(f.path, fo.path) AS path, " "       COALESCE(f.created_at, fo.created_at) AS created_at, " "       COALESCE(f.updated_at, fo.updated_at) AS updated_at " "FROM (" "  SELECT combined.id, combined.name, combined.type " "  FROM (" "    SELECT f.id, f.name, 'file' AS type " "    FROM files f " + file_where + " " "    UNION ALL " "    SELECT fo.id, fo.name, 'folder' AS type " "    FROM folders fo " + folder_where + " " "  ) AS combined " "  ORDER BY " + inner_order_by + " " "  LIMIT $3 OFFSET $4" ") AS page " "LEFT JOIN files f ON page.type = 'file' AND f.id = page.id " "LEFT JOIN folders fo ON page.type = 'folder' AND fo.id = page.id " "LEFT JOIN file_contents fc ON f.content_id = fc.id " "ORDER BY " + outer_order_by;

                if (has_folder_filter) {
                    auto file_count_result = co_await m_db_client->execSqlCoro(
                        file_count_sql,
                        user_id,
                        search_param,
                        *request.folder_id
                    );
                    if (!file_count_result.empty()) {
                        total += static_cast<int>(file_count_result[0]["cnt"].as<int64_t>());
                    }

                    auto folder_count_result = co_await m_db_client->execSqlCoro(
                        folder_count_sql,
                        user_id,
                        search_param,
                        *request.folder_id
                    );
                    if (!folder_count_result.empty()) {
                        total += static_cast<int>(folder_count_result[0]["cnt"].as<int64_t>());
                    }

                    auto result = co_await m_db_client->execSqlCoro(
                        data_sql,
                        user_id,
                        search_param,
                        *request.folder_id,
                        user_id,
                        search_param,
                        *request.folder_id,
                        request.page_size,
                        offset
                    );

                    for (const auto& row : result) {
                        SearchResultItem item;
                        item.id = row["id"].as<uint64_t>();
                        item.name = row["name"].as<std::string>();
                        item.type = row["type"].as<std::string>();
                        item.size = row["size"].as<uint64_t>();
                        item.mime_type = row["mime_type"].as<std::string>();
                        item.hash = row["hash"].as<std::string>();
                        item.item_count = static_cast<int>(row["item_count"].as<int64_t>());
                        item.path = row["path"].as<std::string>();
                        item.created_at = row["created_at"].as<std::string>();
                        item.updated_at = row["updated_at"].as<std::string>();
                        items.push_back(item);
                    }
                } else {
                    auto file_count_result =
                        co_await m_db_client->execSqlCoro(file_count_sql, user_id, search_param);
                    if (!file_count_result.empty()) {
                        total += static_cast<int>(file_count_result[0]["cnt"].as<int64_t>());
                    }

                    auto folder_count_result =
                        co_await m_db_client->execSqlCoro(folder_count_sql, user_id, search_param);
                    if (!folder_count_result.empty()) {
                        total += static_cast<int>(folder_count_result[0]["cnt"].as<int64_t>());
                    }

                    auto result = co_await m_db_client->execSqlCoro(
                        data_sql,
                        user_id,
                        search_param,
                        user_id,
                        search_param,
                        request.page_size,
                        offset
                    );

                    for (const auto& row : result) {
                        SearchResultItem item;
                        item.id = row["id"].as<uint64_t>();
                        item.name = row["name"].as<std::string>();
                        item.type = row["type"].as<std::string>();
                        item.size = row["size"].as<uint64_t>();
                        item.mime_type = row["mime_type"].as<std::string>();
                        item.hash = row["hash"].as<std::string>();
                        item.item_count = static_cast<int>(row["item_count"].as<int64_t>());
                        item.path = row["path"].as<std::string>();
                        item.created_at = row["created_at"].as<std::string>();
                        item.updated_at = row["updated_at"].as<std::string>();
                        items.push_back(item);
                    }
                }

            } else if (request.type == "file") {
                const std::string count_sql =
                    "SELECT COUNT(*) AS cnt FROM files f " + file_where;
                const std::string data_sql =
                    "SELECT f.id, f.name, f.size, f.mime_type, f.path, f.created_at, f.updated_at, " "       COALESCE(fc.hash_md5, '') AS hash " "FROM (" "  SELECT f.id, f.name " "  FROM files f " + file_where + " " "  ORDER BY " + inner_order_by + " " "  LIMIT $3 OFFSET $4" ") AS page " "JOIN files f ON f.id = page.id " "LEFT JOIN file_contents fc ON f.content_id = fc.id " "ORDER BY " + outer_order_by;

                if (has_folder_filter) {
                    auto count_result = co_await m_db_client->execSqlCoro(
                        count_sql,
                        user_id,
                        search_param,
                        *request.folder_id
                    );

                    if (!count_result.empty()) {
                        total = static_cast<int>(count_result[0]["cnt"].as<int64_t>());
                    }

                    auto result = co_await m_db_client->execSqlCoro(
                        data_sql,
                        user_id,
                        search_param,
                        *request.folder_id,
                        request.page_size,
                        offset
                    );

                    for (const auto& row : result) {
                        SearchResultItem item;
                        item.id = row["id"].as<uint64_t>();
                        item.name = row["name"].as<std::string>();
                        item.type = "file";
                        item.size = row["size"].as<uint64_t>();
                        item.mime_type = row["mime_type"].as<std::string>();
                        item.hash = row["hash"].as<std::string>();
                        item.path = row["path"].as<std::string>();
                        item.created_at = row["created_at"].as<std::string>();
                        item.updated_at = row["updated_at"].as<std::string>();
                        items.push_back(item);
                    }
                } else {
                    auto count_result =
                        co_await m_db_client->execSqlCoro(count_sql, user_id, search_param);

                    if (!count_result.empty()) {
                        total = static_cast<int>(count_result[0]["cnt"].as<int64_t>());
                    }

                    auto result = co_await m_db_client->execSqlCoro(
                        data_sql,
                        user_id,
                        search_param,
                        request.page_size,
                        offset
                    );

                    for (const auto& row : result) {
                        SearchResultItem item;
                        item.id = row["id"].as<uint64_t>();
                        item.name = row["name"].as<std::string>();
                        item.type = "file";
                        item.size = row["size"].as<uint64_t>();
                        item.mime_type = row["mime_type"].as<std::string>();
                        item.hash = row["hash"].as<std::string>();
                        item.path = row["path"].as<std::string>();
                        item.created_at = row["created_at"].as<std::string>();
                        item.updated_at = row["updated_at"].as<std::string>();
                        items.push_back(item);
                    }
                }

            } else if (request.type == "folder") {
                const std::string count_sql =
                    "SELECT COUNT(*) AS cnt FROM folders fo " + folder_where;
                const std::string data_sql =
                    "SELECT fo.id, fo.name, fo.item_count, fo.path, fo.created_at, fo.updated_at " "FROM (" "  SELECT fo.id, fo.name " "  FROM folders fo " + folder_where + " " "  ORDER BY " + inner_order_by + " " "  LIMIT $3 OFFSET $4" ") AS page " "JOIN folders fo ON fo.id = page.id " "ORDER BY " + outer_order_by;

                if (has_folder_filter) {
                    auto count_result = co_await m_db_client->execSqlCoro(
                        count_sql,
                        user_id,
                        search_param,
                        *request.folder_id
                    );

                    if (!count_result.empty()) {
                        total = static_cast<int>(count_result[0]["cnt"].as<int64_t>());
                    }

                    auto result = co_await m_db_client->execSqlCoro(
                        data_sql,
                        user_id,
                        search_param,
                        *request.folder_id,
                        request.page_size,
                        offset
                    );

                    for (const auto& row : result) {
                        SearchResultItem item;
                        item.id = row["id"].as<uint64_t>();
                        item.name = row["name"].as<std::string>();
                        item.type = "folder";
                        item.item_count = static_cast<int>(row["item_count"].as<int64_t>());
                        item.path = row["path"].as<std::string>();
                        item.created_at = row["created_at"].as<std::string>();
                        item.updated_at = row["updated_at"].as<std::string>();
                        items.push_back(item);
                    }
                } else {
                    auto count_result =
                        co_await m_db_client->execSqlCoro(count_sql, user_id, search_param);

                    if (!count_result.empty()) {
                        total = static_cast<int>(count_result[0]["cnt"].as<int64_t>());
                    }

                    auto result = co_await m_db_client->execSqlCoro(
                        data_sql,
                        user_id,
                        search_param,
                        request.page_size,
                        offset
                    );

                    for (const auto& row : result) {
                        SearchResultItem item;
                        item.id = row["id"].as<uint64_t>();
                        item.name = row["name"].as<std::string>();
                        item.type = "folder";
                        item.item_count = static_cast<int>(row["item_count"].as<int64_t>());
                        item.path = row["path"].as<std::string>();
                        item.created_at = row["created_at"].as<std::string>();
                        item.updated_at = row["updated_at"].as<std::string>();
                        items.push_back(item);
                    }
                }
            }

            total_pages = request.page_size > 0 ? (total + request.page_size - 1) / request.page_size : 0;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "Failed to search: " << e.base().what();
        }

        SearchResponse response;
        response.items = items;
        response.pagination = { .page = request.page,
                                .page_size = request.page_size,
                                .total = total,
                                .total_pages = total_pages };

        Logger::Debug() << "Search completed: total=" << total << ", page=" << request.page;
        co_return response;
    }

    /// ==================== 私有辅助方法 ====================

    auto FileService::CheckStorageQuota(uint64_t user_id, uint64_t file_size) const
        -> drogon::Task<Result<void>> {
        co_return co_await CheckStorageQuota(m_db_client, user_id, file_size);
    }

    auto FileService::ReserveStorageQuota(uint64_t user_id, uint64_t file_size) const
        -> drogon::Task<Result<void>> {

        try {
            auto result = co_await m_db_client->execSqlCoro(
                "UPDATE users SET storage_reserved = storage_reserved + $1 " "WHERE id = $2 AND storage_used + storage_reserved + $3 <= storage_quota",
                file_size,
                user_id,
                file_size
            );

            if (result.affectedRows() == 0) {
                Logger::Warn() << "Insufficient storage quota for reservation: user_id=" << user_id
                         << ", file_size=" << file_size;
                co_return std::unexpected(ErrorInfo(ErrorCode::StorageQuotaExceeded));
            }

            Logger::Debug() << "Storage quota reserved: user_id=" << user_id
                      << ", file_size=" << file_size;
            co_return {};

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to reserve storage quota: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to reserve storage quota")
            );
        }
    }

    auto FileService::ReleaseReservedQuota(uint64_t user_id, uint64_t reserved_bytes)
        -> drogon::Task<void> {

        if (reserved_bytes == 0) {
            co_return;
        }

        try {
            co_await m_db_client->execSqlCoro(
                "UPDATE users SET storage_reserved = GREATEST(storage_reserved - $1, 0) WHERE id = $2",
                reserved_bytes,
                user_id
            );

            Logger::Debug() << "Reserved quota released: user_id=" << user_id
                      << ", reserved_bytes=" << reserved_bytes;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to release reserved quota: " << e.base().what();
        }
    }

    auto FileService::FindExistingContent(const std::string& file_hash) const
        -> drogon::Task<std::optional<uint64_t>> {

        try {
            CoroMapper<FileContents> mapper(m_db_client);
            auto content = co_await mapper.findOne(
                Criteria(FileContents::Cols::_hash_md5, CompareOperator::EQ, file_hash)
            );

            co_return content.getValueOfId();

        } catch (const drogon::orm::DrogonDbException&) {
            co_return std::nullopt;
        }
    }

    auto FileService::FindExistingTask(uint64_t user_id, const std::string& file_hash) const
        -> drogon::Task<std::optional<UploadTasks>> {

        try {
            CoroMapper<UploadTasks> mapper(m_db_client);
            auto task = co_await mapper.findOne(
                Criteria(UploadTasks::Cols::_user_id, CompareOperator::EQ, user_id) &&
                Criteria(UploadTasks::Cols::_file_hash, CompareOperator::EQ, file_hash) &&
                Criteria(UploadTasks::Cols::_status, CompareOperator::EQ, 0)
            );

            co_return task;

        } catch (const drogon::orm::DrogonDbException&) {
            co_return std::nullopt;
        }
    }

    auto FileService::FindUploadTask(const std::string& upload_id, uint64_t user_id) const
        -> drogon::Task<Result<UploadTasks>> {

        try {
            CoroMapper<UploadTasks> mapper(m_db_client);
            auto task = co_await mapper.findByPrimaryKey(upload_id);

            if (task.getValueOfUserId() != user_id) {
                Logger::Warn() << "Upload task does not belong to current user: upload_id=" << upload_id
                         << ", task_user_id=" << task.getValueOfUserId()
                         << ", request_user_id=" << user_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::UploadTaskNotFound));
            }

            co_return task;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to query upload task: " << e.base().what();
            co_return std::unexpected(ErrorInfo(ErrorCode::UploadTaskNotFound));
        }
    }

    auto FileService::BuildUploadTaskCacheEntry(const UploadTasks& task) -> UploadTaskCacheEntry {
        return UploadTaskCacheEntry{ .user_id = task.getValueOfUserId(),
                                     .file_size = task.getValueOfFileSize(),
                                     .chunk_size = task.getValueOfChunkSize(),
                                     .total_chunks = task.getValueOfTotalChunks(),
                                     .expires_at = task.getValueOfExpiresAt(),
                                     .status = task.getValueOfStatus(),
                                     .file_hash = task.getValueOfFileHash(),
                                     .filename = task.getValueOfFilename(),
                                     .parent_id = task.getValueOfFolderId(),
                                     .cache_expires_at = std::chrono::steady_clock::now() + UPLOAD_TASK_CACHE_TTL };
    }

    auto FileService::TryGetUploadTaskCacheEntry(const std::string& upload_id, uint64_t user_id)
        -> std::optional<UploadTaskCacheEntry> {

        const auto now = std::chrono::steady_clock::now();
        {
            /// 读路径使用共享锁，降低高频分片上传之间的锁竞争。
            std::shared_lock<std::shared_mutex> lock(m_upload_task_cache_mutex);
            auto it = m_upload_task_cache.find(upload_id);
            if (it == m_upload_task_cache.end()) {
                return std::nullopt;
            }

            if (it->second.cache_expires_at > now && it->second.user_id == user_id) {
                return it->second;
            }

            if (it->second.cache_expires_at > now) {
                return std::nullopt;
            }
        }

        /// 仅在确认缓存过期后切换到写锁做清理，避免读路径长时间持有独占锁。
        std::unique_lock<std::shared_mutex> lock(m_upload_task_cache_mutex);
        auto it = m_upload_task_cache.find(upload_id);
        if (it == m_upload_task_cache.end()) {
            return std::nullopt;
        }
        if (it->second.cache_expires_at <= now) {
            m_upload_task_cache.erase(it);
        }
        return std::nullopt;
    }

    auto FileService::CacheUploadTaskEntry(const std::string& upload_id, UploadTaskCacheEntry entry)
        -> void {

        std::unique_lock<std::shared_mutex> lock(m_upload_task_cache_mutex);
        m_upload_task_cache[upload_id] = std::move(entry);
    }

    auto FileService::InvalidateUploadTaskCache(const std::string& upload_id) -> void {
        std::unique_lock<std::shared_mutex> lock(m_upload_task_cache_mutex);
        m_upload_task_cache.erase(upload_id);
    }

    auto FileService::StartUploadTaskCacheMaintenance() -> void {
        if (auto* loop = drogon::app().getLoop(); loop != nullptr) {
            loop->runEvery(
                UPLOAD_TASK_CACHE_MAINTENANCE_INTERVAL_SECONDS,
                [this]() { EvictExpiredUploadTaskCacheEntries(); }
            );
            Logger::Debug() << "Upload task cache maintenance timer started (interval="
                      << UPLOAD_TASK_CACHE_MAINTENANCE_INTERVAL_SECONDS << "s)";
        }
    }

    auto FileService::EvictExpiredUploadTaskCacheEntries() -> void {
        const auto now = std::chrono::steady_clock::now();
        std::unique_lock<std::shared_mutex> lock(m_upload_task_cache_mutex);
        for (auto it = m_upload_task_cache.begin(); it != m_upload_task_cache.end();) {
            if (it->second.cache_expires_at <= now) {
                it = m_upload_task_cache.erase(it);
            } else {
                ++it;
            }
        }
    }

    auto FileService::UpdateStorageUsed(uint64_t user_id, int64_t delta) -> drogon::Task<void> {
        co_await UpdateStorageUsed(m_db_client, user_id, delta);
    }

    auto FileService::IsFilenameExists(
        uint64_t folder_id,
        const std::string& filename,
        uint64_t user_id
    ) const -> drogon::Task<bool> {

        try {
            CoroMapper<Files> mapper(m_db_client);
            auto count = co_await mapper.count(
                Criteria(Files::Cols::_user_id, CompareOperator::EQ, user_id) &&
                Criteria(Files::Cols::_folder_id, CompareOperator::EQ, folder_id) &&
                Criteria(Files::Cols::_name, CompareOperator::EQ, filename)
            );

            co_return count > 0;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to check filename: " << e.base().what();
            co_return false;
        }
    }

    auto FileService::ExtractExtension(const std::string& filename) -> std::string {
        auto pos = filename.rfind('.');
        if (pos == std::string::npos || pos == filename.length() - 1) {
            return "";
        }
        return filename.substr(pos + 1);
    }

    auto FileService::IsImageMimeType(const std::string& mime_type) -> bool {
        return mime_type.starts_with("image/");
    }

    /// ── 事务感知辅助方法实现 ──

    auto FileService::CheckStorageQuota(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        uint64_t file_size
    ) const -> drogon::Task<Result<void>> {

        try {
            auto result = co_await client->execSqlCoro(
                "UPDATE users SET storage_used = storage_used + $1 " "WHERE id = $2 AND storage_used + $3 <= storage_quota",
                file_size,
                user_id,
                file_size
            );

            if (result.affectedRows() == 0) {
                Logger::Warn() << "Insufficient storage space: user_id=" << user_id
                         << ", file_size=" << file_size;
                co_return std::unexpected(ErrorInfo(ErrorCode::StorageQuotaExceeded));
            }

            Logger::Debug() << "Storage quota check passed and reserved: user_id=" << user_id
                      << ", file_size=" << file_size;
            co_return {};

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to reserve user storage quota: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to reserve storage quota")
            );
        }
    }

    auto FileService::UpdateStorageUsed(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        int64_t delta
    ) -> drogon::Task<void> {

        try {
            if (delta >= 0) {
                auto result = co_await client->execSqlCoro(
                    "UPDATE users SET storage_used = storage_used + $1 " "WHERE id = $2 AND storage_used + $3 <= storage_quota",
                    delta,
                    user_id,
                    delta
                );

                if (result.affectedRows() == 0) {
                    Logger::Warn() << "Skipped storage usage increment due to quota limit: user_id="
                             << user_id << ", delta=" << delta;
                    co_return;
                }
            } else {
                co_await client->execSqlCoro(
                    "UPDATE users SET storage_used = GREATEST(storage_used + $1, 0) WHERE id = $2",
                    delta,
                    user_id
                );
            }

            Logger::Debug() << "Storage usage updated: user_id=" << user_id << ", delta=" << delta;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to update storage usage: " << e.base().what();
        }
    }

    auto FileService::IncrementContentRefCount(
        const drogon::orm::DbClientPtr& client,
        const std::unordered_map<uint64_t, uint64_t>& content_ref_increment,
        const std::unordered_set<uint64_t>& existing_content_ids
    ) -> drogon::Task<std::unordered_set<uint64_t>> {

        std::string update_sql = "UPDATE file_contents SET ref_count = ref_count + CASE id ";
        std::vector<std::pair<uint64_t, uint64_t>> update_cases;
        update_cases.reserve(content_ref_increment.size());
        std::vector<uint64_t> valid_content_ids;
        valid_content_ids.reserve(content_ref_increment.size());

        int param_index = 1;
        for (const auto& [content_id, increment] : content_ref_increment) {
            if (!existing_content_ids.contains(content_id)) {
                continue;
            }
            update_cases.emplace_back(content_id, increment);
            valid_content_ids.push_back(content_id);
            auto when_param = std::to_string(param_index++);
            auto then_param = std::to_string(param_index++);
            update_sql += " WHEN $" + when_param + " THEN $" + then_param;
        }

        std::unordered_set<uint64_t> incremented_ids;
        incremented_ids.reserve(valid_content_ids.size());

        if (!valid_content_ids.empty()) {
            update_sql += " ELSE 0 END WHERE id IN (" +
                          BatchUtils::BuildSafeNumericInClause(valid_content_ids) + ")";

            try {
                co_await ExecSqlWithBindings(
                    client,
                    update_sql,
                    [&](auto& binder) {
                        for (const auto& [content_id, increment] : update_cases) {
                            binder << content_id << increment;
                        }
                    }
                );
                for (const auto id : valid_content_ids) {
                    incremented_ids.insert(id);
                }
            } catch (const drogon::orm::DrogonDbException& e) {
                Logger::Warn() << "File content batch ref_count update failed: " << e.base().what();
            }
        }

        co_return incremented_ids;
    }

    auto FileService::InsertCopiedFiles(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        uint64_t target_folder_id,
        const std::vector<std::pair<uint64_t, const drogon_model::disk::Files*>>& valid_items
    ) -> drogon::Task<std::vector<std::pair<uint64_t, uint64_t>>> {

        std::vector<std::pair<uint64_t, uint64_t>> id_mappings;

        if (valid_items.empty()) {
            co_return id_mappings;
        }

        auto target_location_result = co_await ResolveFolderLocation(client, target_folder_id, user_id);
        if (!target_location_result) {
            co_return id_mappings;
        }

        std::string insert_sql =
            "INSERT INTO files (user_id, content_id, folder_id, name, extension, " "size, mime_type, path, is_favorite, download_count) VALUES ";

        int param_index = 1;
        for (size_t i = 0; i < valid_items.size(); ++i) {
            if (i > 0) {
                insert_sql += ",";
            }
            insert_sql += "(";
            for (int j = 0; j < 10; ++j) {
                if (j > 0) {
                    insert_sql += ",";
                }
                insert_sql += "$" + std::to_string(param_index++);
            }
            insert_sql += ")";
        }
        insert_sql += " RETURNING id";

        try {
            auto result = co_await ExecSqlWithBindings(
                client,
                insert_sql,
                [&](auto& binder) {
                    for (const auto& [old_id, file_ptr] : valid_items) {
                        (void)old_id;
                        const auto& file = *file_ptr;
                        auto content_id_ptr = file.getContentId();
                        auto content_id = content_id_ptr ? std::optional<uint64_t>(*content_id_ptr) : std::nullopt;

                        binder << user_id << content_id << target_folder_id
                               << file.getValueOfName() << file.getValueOfExtension()
                               << file.getValueOfSize() << file.getValueOfMimeType()
                               << BuildFilePath(target_location_result->path, file.getValueOfName())
                               << 0 << 0;
                    }
                }
            );

            if (result.size() == valid_items.size()) {
                for (size_t i = 0; i < valid_items.size(); ++i) {
                    uint64_t new_id = result[i]["id"].as<uint64_t>();
                    id_mappings.emplace_back(valid_items[i].first, new_id);
                }
            }
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Batch file insert failed in copy: " << e.base().what();
        }

        co_return id_mappings;
    }

    auto FileService::DeleteFilesByIds(
        const drogon::orm::DbClientPtr& client,
        const std::vector<uint64_t>& file_ids
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
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "Batch file delete failed: " << e.base().what();
            co_return 0;
        }
    }

    auto FileService::LookupExistingContentMetadata(
        const drogon::orm::DbClientPtr& client,
        const std::string& file_hash
    ) const -> drogon::Task<std::optional<ExistingContentMetadata>> {

        try {
            auto result = co_await client->execSqlCoro(
                "SELECT id, mime_type FROM file_contents WHERE hash_md5 = $1 LIMIT 1",
                file_hash
            );

            if (result.empty()) {
                co_return std::nullopt;
            }

            ExistingContentMetadata metadata;
            metadata.id = result[0]["id"].as<uint64_t>();
            metadata.mime_type = result[0]["mime_type"].as<std::string>();
            co_return metadata;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to lookup existing content metadata: " << e.base().what();
            co_return std::nullopt;
        }
    }

    auto FileService::IsFilenameExists(
        const drogon::orm::DbClientPtr& client,
        uint64_t folder_id,
        const std::string& filename,
        uint64_t user_id
    ) const -> drogon::Task<bool> {

        try {
            auto result = co_await client->execSqlCoro(
                "SELECT COUNT(*) AS cnt FROM files " "WHERE user_id = $1 AND folder_id = $2 AND name = $3",
                user_id,
                folder_id,
                filename
            );

            if (!result.empty()) {
                co_return result[0]["cnt"].as<uint64_t>() > 0;
            }
            co_return false;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to check filename (transaction): " << e.base().what();
            co_return false;
        }
    }

    auto FileService::GetUploadedChunkCoverage(
        const drogon::orm::DbClientPtr& client,
        const std::string& upload_id
    ) const -> drogon::Task<std::optional<UploadedChunkCoverage>> {

        try {
            auto result = co_await client->execSqlCoro(
                "SELECT COUNT(*) AS uploaded_count, " "COALESCE(MAX(chunk_index), -1) AS max_chunk_index " "FROM upload_task_chunks WHERE task_id = $1",
                upload_id
            );

            if (result.empty()) {
                co_return UploadedChunkCoverage{ 0, -1 };
            }

            UploadedChunkCoverage coverage;
            coverage.uploaded_count = result[0]["uploaded_count"].as<uint64_t>();
            coverage.max_chunk_index = result[0]["max_chunk_index"].as<int64_t>();
            co_return coverage;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to get uploaded chunk coverage: " << e.base().what();
            co_return std::nullopt;
        }
    }

} ///< namespace disk::file
