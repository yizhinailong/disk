/**
 * @file TrashService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 回收站服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "TrashService.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include <drogon/orm/CoroMapper.h>
#include <drogon/orm/Criteria.h>
#include <json/writer.h>

#include "models/Files.hpp"
#include "models/Folders.hpp"
#include "models/Trash.hpp"
#include "models/Users.hpp"
#include "services/ContentService.hpp"
#include "services/FileListCache.hpp"
#include "services/FileRepository.hpp"
#include "services/FolderRepository.hpp"
#include "services/QuotaService.hpp"
#include "services/TransactionRunner.hpp"
#include "services/TrashContentIdResolver.hpp"
#include "utils/BatchUtils.hpp"

namespace disk::trash {

    using disk::utils::BatchUtils;
    using disk::utils::DEFAULT_BATCH_CHUNK_SIZE;
    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::Files;
    using drogon_model::disk::Folders;
    using drogon_model::disk::Trash;
    using drogon_model::disk::Users;

    struct SnapshotFolder {
        uint64_t id{ 0 };
        uint64_t parent_id{ 0 };
        std::string name;
        uint32_t item_count{ 0 };
        uint32_t depth{ 0 };
    };

    struct SnapshotFile {
        uint64_t id{ 0 };
        uint64_t folder_id{ 0 };
        uint64_t content_id{ 0 };
        std::string name;
        std::string extension;
        uint64_t size{ 0 };
        std::string mime_type;
        bool is_favorite{ false };
        uint32_t download_count{ 0 };
    };

    struct FolderTreeSnapshot {
        SnapshotFolder root;
        std::vector<SnapshotFolder> folders;
        std::vector<SnapshotFile> files;
    };

    [[nodiscard]] auto ParseFolderTreeSnapshot(const std::string& item_data)
        -> std::optional<FolderTreeSnapshot> {
        if (item_data.empty()) {
            return std::nullopt;
        }

        Json::Value json;
        Json::Reader reader;
        if (!reader.parse(item_data, json) || json.get("type", "").asString() != "folder_tree") {
            return std::nullopt;
        }

        FolderTreeSnapshot snapshot;
        const auto& root = json["root"];
        if (!root.isObject() || !root.isMember("id")) {
            return std::nullopt;
        }

        snapshot.root.id = root["id"].asUInt64();
        snapshot.root.parent_id = root.get("parent_id", 0).asUInt64();
        snapshot.root.name = root.get("name", "").asString();
        snapshot.root.item_count = root.get("item_count", 0).asUInt();
        snapshot.root.depth = root.get("depth", 0).asUInt();

        const auto& folders = json["folders"];
        if (folders.isArray()) {
            snapshot.folders.reserve(folders.size());
            for (const auto& folder : folders) {
                SnapshotFolder value;
                value.id = folder.get("id", 0).asUInt64();
                value.parent_id = folder.get("parent_id", 0).asUInt64();
                value.name = folder.get("name", "").asString();
                value.item_count = folder.get("item_count", 0).asUInt();
                value.depth = folder.get("depth", 0).asUInt();
                if (value.id > 0 && !value.name.empty()) {
                    snapshot.folders.push_back(std::move(value));
                }
            }
        }

        std::sort(snapshot.folders.begin(), snapshot.folders.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.id < rhs.id;
        });

        const auto& files = json["files"];
        if (files.isArray()) {
            snapshot.files.reserve(files.size());
            for (const auto& file : files) {
                if (!file.isMember("content_id")) {
                    continue;
                }

                SnapshotFile value;
                value.id = file.get("id", 0).asUInt64();
                value.folder_id = file.get("folder_id", 0).asUInt64();
                value.content_id = file["content_id"].asUInt64();
                value.name = file.get("name", "").asString();
                value.extension = file.get("extension", "").asString();
                value.size = file.get("size", 0).asUInt64();
                value.mime_type = file.get("mime_type", "application/octet-stream").asString();
                value.is_favorite = file.get("is_favorite", 0).asBool();
                value.download_count = file.get("download_count", 0).asUInt();
                if (value.id > 0 && value.folder_id > 0 && value.content_id > 0 && !value.name.empty()) {
                    snapshot.files.push_back(std::move(value));
                }
            }
        }

        if (snapshot.root.id == 0 || snapshot.root.name.empty()) {
            return std::nullopt;
        }

        return snapshot;
    }

    [[nodiscard]] auto ExtractSnapshotContentIds(const std::string& item_data)
        -> std::vector<uint64_t> {
        std::vector<uint64_t> content_ids;
        auto snapshot = ParseFolderTreeSnapshot(item_data);
        if (!snapshot.has_value()) {
            return content_ids;
        }

        content_ids.reserve(snapshot->files.size());
        for (const auto& file : snapshot->files) {
            content_ids.push_back(file.content_id);
        }
        return content_ids;
    }

    TrashService::TrashService(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)), m_trash_query(m_db_client) {
        Logger::Debug(disk::utils::ServiceRuntimeLogContext()) << "Service initialized: service=trash";
    }

    auto TrashService::CreateTrashRecords(
        const drogon::orm::DbClientPtr& client,
        const std::vector<disk::file::utils::TrashInsertItem>& trash_items,
        uint64_t user_id,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<bool> {
        co_return co_await disk::file::utils::InsertTrashRecords(
            client,
            trash_items,
            user_id,
            log_context
        );
    }

    auto TrashService::MoveToTrash(
        MoveToTrashRequest request,
        uint64_t user_id,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<MoveToTrashResult>> {

        auto normalize_ids = [](std::vector<uint64_t> ids) {
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            return ids;
        };

        auto requested_file_ids = normalize_ids(std::move(request.file_ids));
        auto requested_folder_ids = normalize_ids(std::move(request.folder_ids));

        int deleted_file_count = 0;
        int deleted_folder_count = 0;
        std::vector<uint64_t> file_ids_to_delete;
        std::vector<uint64_t> folder_ids_to_delete;
        disk::file::TransactionRunner transaction_runner(m_db_client, log_context);
        auto tx_result = co_await transaction_runner.Run(
            [&](const drogon::orm::DbClientPtr& transaction) -> drogon::Task<Result<void>> {
                disk::folder::FolderRepository folder_repository;
                std::unordered_set<uint64_t> locked_folder_ids;
                std::unordered_map<uint64_t, disk::file::utils::FolderDeletePlan> folder_plans;
                std::vector<uint64_t> top_level_folder_ids;

                while (true) {
                    folder_plans = co_await disk::file::utils::FetchBatchFolderDeletePlans(
                        transaction,
                        requested_folder_ids,
                        user_id
                    );
                    top_level_folder_ids = disk::file::utils::FilterCoveredFolderIds(
                        requested_folder_ids,
                        folder_plans
                    );

                    std::vector<uint64_t> discovered_folder_ids;
                    for (const auto folder_id : top_level_folder_ids) {
                        auto plan_it = folder_plans.find(folder_id);
                        if (plan_it == folder_plans.end()) {
                            continue;
                        }
                        for (const auto& folder : plan_it->second.folders) {
                            discovered_folder_ids.push_back(folder.getValueOfId());
                        }
                    }
                    discovered_folder_ids = normalize_ids(std::move(discovered_folder_ids));

                    std::vector<uint64_t> folder_ids_to_lock;
                    folder_ids_to_lock.reserve(discovered_folder_ids.size());
                    for (const auto folder_id : discovered_folder_ids) {
                        if (!locked_folder_ids.contains(folder_id)) {
                            folder_ids_to_lock.push_back(folder_id);
                        }
                    }
                    if (folder_ids_to_lock.empty()) {
                        break;
                    }

                    auto newly_locked_folders = co_await folder_repository.FetchOwnedFoldersByIdsForUpdate(
                        transaction,
                        folder_ids_to_lock,
                        user_id
                    );
                    (void)newly_locked_folders;
                    locked_folder_ids.insert(folder_ids_to_lock.begin(), folder_ids_to_lock.end());
                }

                auto covered_file_ids = disk::file::utils::CollectCoveredFileIds(
                    top_level_folder_ids,
                    folder_plans
                );
                disk::file::FileRepository file_repository;
                std::vector<uint64_t> file_ids_to_lock = requested_file_ids;
                file_ids_to_lock.insert(
                    file_ids_to_lock.end(),
                    covered_file_ids.begin(),
                    covered_file_ids.end()
                );
                file_ids_to_lock = normalize_ids(std::move(file_ids_to_lock));
                auto locked_files = co_await file_repository.FetchOwnedFilesByIdsForUpdate(
                    transaction,
                    file_ids_to_lock,
                    user_id
                );
                std::unordered_map<uint64_t, const Files*> file_map;
                file_map.reserve(locked_files.size());
                for (const auto& file : locked_files) {
                    file_map[file.getValueOfId()] = &file;
                }

                for (const auto folder_id : top_level_folder_ids) {
                    auto plan_it = folder_plans.find(folder_id);
                    if (plan_it == folder_plans.end()) {
                        continue;
                    }

                    auto& plan = plan_it->second;
                    std::unordered_set<uint64_t> plan_folder_ids;
                    plan_folder_ids.reserve(plan.folders.size());
                    for (const auto& folder : plan.folders) {
                        plan_folder_ids.insert(folder.getValueOfId());
                    }

                    plan.files.clear();
                    plan.item_size = 0;
                    for (const auto& file : locked_files) {
                        if (!plan_folder_ids.contains(file.getValueOfFolderId())) {
                            continue;
                        }
                        plan.files.push_back(file);
                        plan.item_size += file.getValueOfSize();
                    }
                }

                covered_file_ids = disk::file::utils::CollectCoveredFileIds(
                    top_level_folder_ids,
                    folder_plans
                );
                std::vector<uint64_t> explicit_file_ids;
                explicit_file_ids.reserve(requested_file_ids.size());
                for (const auto file_id : requested_file_ids) {
                    if (covered_file_ids.contains(file_id)) {
                        Logger::Debug(log_context)
                            << "Skipping explicit file delete covered by folder delete: file_id="
                            << file_id;
                        continue;
                    }
                    explicit_file_ids.push_back(file_id);
                }

                std::vector<disk::file::utils::TrashInsertItem> trash_items;
                trash_items.reserve(file_map.size() + top_level_folder_ids.size());
                std::unordered_map<uint64_t, int> parent_deltas;
                file_ids_to_delete.clear();
                folder_ids_to_delete.clear();
                file_ids_to_delete.reserve(file_map.size() + covered_file_ids.size());
                deleted_file_count = 0;
                deleted_folder_count = 0;

                for (const auto file_id : explicit_file_ids) {
                    auto it = file_map.find(file_id);
                    if (it == file_map.end()) {
                        Logger::Warn(log_context) << "File not found or delete failed, skipping: file_id=" << file_id;
                        continue;
                    }

                    const auto& file = *it->second;
                    Json::Value item_data;
                    if (file.getContentId()) {
                        item_data["content_id"] = static_cast<Json::UInt64>(*file.getContentId());
                    }
                    item_data["mime_type"] = file.getValueOfMimeType();
                    Json::StreamWriterBuilder builder;
                    builder["indentation"] = "";

                    trash_items.push_back({
                        .item_type = "file",
                        .item_id = static_cast<uint64_t>(file.getValueOfId()),
                        .item_name = file.getValueOfName(),
                        .item_size = static_cast<uint64_t>(file.getValueOfSize()),
                        .original_folder_id = static_cast<uint64_t>(file.getValueOfFolderId()),
                        .original_path = file.getValueOfPath(),
                        .content_id = file.getContentId() ? std::optional<uint64_t>(*file.getContentId()) : std::nullopt,
                        .item_data = Json::writeString(builder, item_data),
                    });
                    file_ids_to_delete.push_back(file.getValueOfId());
                    if (file.getValueOfFolderId() > 0) {
                        --parent_deltas[file.getValueOfFolderId()];
                    }
                    ++deleted_file_count;
                }

                for (const auto folder_id : top_level_folder_ids) {
                    auto plan_it = folder_plans.find(folder_id);
                    if (plan_it == folder_plans.end()) {
                        continue;
                    }

                    const auto& plan = plan_it->second;
                    trash_items.push_back({
                        .item_type = "folder",
                        .item_id = static_cast<uint64_t>(plan.root.getValueOfId()),
                        .item_name = plan.root.getValueOfName(),
                        .item_size = plan.item_size,
                        .original_folder_id = static_cast<uint64_t>(plan.root.getValueOfParentId()),
                        .original_path = plan.root.getValueOfPath(),
                        .content_id = std::nullopt,
                        .item_data = disk::file::utils::BuildFolderSnapshot(plan),
                    });
                    if (plan.root.getValueOfParentId() > 0) {
                        --parent_deltas[plan.root.getValueOfParentId()];
                    }

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

                auto insert_ok = co_await CreateTrashRecords(
                    transaction,
                    trash_items,
                    user_id,
                    log_context
                );
                if (!insert_ok) {
                    co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Failed to delete items"));
                }

                std::vector<std::pair<uint64_t, int>> ordered_parent_deltas(
                    parent_deltas.begin(),
                    parent_deltas.end()
                );
                std::sort(ordered_parent_deltas.begin(), ordered_parent_deltas.end());
                for (const auto& [parent_id, delta] : ordered_parent_deltas) {
                    auto parent_updated = co_await folder_repository.ApplyItemCountDelta(
                        transaction,
                        parent_id,
                        user_id,
                        delta,
                        trantor::Date::now()
                    );
                    if (!parent_updated) {
                        co_return std::unexpected(
                            ErrorInfo(ErrorCode::InternalError, "Failed to delete items")
                        );
                    }
                }

                auto share_stats = co_await CleanupShareLinksForMovedItems(
                    transaction,
                    file_ids_to_delete,
                    folder_ids_to_delete
                );
                Logger::Debug(log_context) << "Cleaned share links during delete: file_links="
                                           << share_stats.deleted_file_share_links
                                           << ", folder_links=" << share_stats.deleted_folder_share_links
                                           << ", cancelled_empty_shares=" << share_stats.cancelled_empty_shares;

                auto deleted_file_rows = co_await disk::file::utils::DeleteFilesByIds(
                    transaction,
                    file_ids_to_delete,
                    log_context
                );
                if (deleted_file_rows != static_cast<int>(file_ids_to_delete.size())) {
                    co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Failed to delete items"));
                }

                auto deleted_folder_rows = co_await disk::file::utils::DeleteFoldersByIds(
                    transaction,
                    folder_ids_to_delete,
                    log_context
                );
                if (deleted_folder_rows != static_cast<int>(folder_ids_to_delete.size())) {
                    co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Failed to delete items"));
                }

                co_return {};
            }
        );
        if (!tx_result) {
            if (tx_result.error().code == ErrorCode::FileNotFound) {
                co_return std::unexpected(tx_result.error());
            }
            Logger::Error(log_context) << "Delete transaction failed: " << tx_result.error().message;
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Failed to delete items"));
        }

        MoveToTrashResult result;
        result.deleted_file_count = deleted_file_count;
        result.deleted_folder_count = deleted_folder_count;
        result.deleted_count = deleted_file_count + deleted_folder_count;
        result.removed_file_ids = std::move(file_ids_to_delete);
        result.removed_folder_ids = std::move(folder_ids_to_delete);

        co_await disk::file::FileListCache::Invalidate(
            m_redis_service,
            user_id,
            log_context
        );

        co_return result;
    }

    auto TrashService::CleanupShareLinksForMovedItems(
        const drogon::orm::DbClientPtr& client,
        const std::vector<uint64_t>& file_ids,
        const std::vector<uint64_t>& folder_ids
    ) const -> drogon::Task<ShareCleanupStats> {
        auto normalize_ids = [](std::vector<uint64_t> ids) {
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            return ids;
        };

        ShareCleanupStats stats;
        std::vector<uint64_t> affected_share_ids;

        auto file_share_chunks = BatchUtils::Chunk(file_ids, DEFAULT_BATCH_CHUNK_SIZE);
        for (const auto& chunk : file_share_chunks) {
            if (chunk.empty()) {
                continue;
            }
            auto linked_shares = co_await client->execSqlCoro(
                "SELECT DISTINCT share_id FROM share_files WHERE item_type = 'file' AND item_id IN (" +
                BatchUtils::BuildSafeNumericInClause(chunk) + ")"
            );
            for (const auto& row : linked_shares) {
                affected_share_ids.push_back(row["share_id"].as<uint64_t>());
            }
            auto result = co_await client->execSqlCoro(
                "DELETE FROM share_files WHERE item_type = 'file' AND item_id IN (" +
                BatchUtils::BuildSafeNumericInClause(chunk) + ")"
            );
            stats.deleted_file_share_links += static_cast<int>(result.affectedRows());
        }

        auto folder_share_chunks = BatchUtils::Chunk(folder_ids, DEFAULT_BATCH_CHUNK_SIZE);
        for (const auto& chunk : folder_share_chunks) {
            if (chunk.empty()) {
                continue;
            }
            auto linked_shares = co_await client->execSqlCoro(
                "SELECT DISTINCT share_id FROM share_files WHERE item_type = 'folder' AND item_id IN (" +
                BatchUtils::BuildSafeNumericInClause(chunk) + ")"
            );
            for (const auto& row : linked_shares) {
                affected_share_ids.push_back(row["share_id"].as<uint64_t>());
            }
            auto result = co_await client->execSqlCoro(
                "DELETE FROM share_files WHERE item_type = 'folder' AND item_id IN (" +
                BatchUtils::BuildSafeNumericInClause(chunk) + ")"
            );
            stats.deleted_folder_share_links += static_cast<int>(result.affectedRows());
        }

        affected_share_ids = normalize_ids(std::move(affected_share_ids));
        auto affected_share_chunks = BatchUtils::Chunk(affected_share_ids, DEFAULT_BATCH_CHUNK_SIZE);
        for (const auto& chunk : affected_share_chunks) {
            if (chunk.empty()) {
                continue;
            }
            auto result = co_await client->execSqlCoro(
                "UPDATE shares s SET status = 0, updated_at = NOW() " "WHERE s.status = 1 AND s.id IN (" + BatchUtils::BuildSafeNumericInClause(chunk) + ") " "AND NOT EXISTS (SELECT 1 FROM share_files sf WHERE sf.share_id = s.id)"
            );
            stats.cancelled_empty_shares += static_cast<int>(result.affectedRows());
        }

        co_return stats;
    }

    auto TrashService::CleanupExpiredTrashItems(
        int fetch_batch_size,
        int max_batches_per_run,
        disk::utils::LogContext log_context
    ) -> drogon::Task<Result<int>> {
        Logger::Info(log_context) << "Starting cleanup of expired trash items";

        if (fetch_batch_size <= 0 || max_batches_per_run <= 0) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InvalidParameter, "Trash cleanup bounds must be positive")
            );
        }

        int deleted_count = 0;
        uint64_t after_id = 0;
        int batch_iteration = 0;
        while (batch_iteration < max_batches_per_run) {
            auto page = co_await CleanupExpiredTrashPage(
                after_id,
                static_cast<size_t>(fetch_batch_size),
                log_context
            );
            if (!page) {
                co_return std::unexpected(page.error());
            }

            deleted_count += static_cast<int>(page->deleted);
            batch_iteration++;
            if (!page->has_more) {
                break;
            }
            if (page->next_after_id <= after_id) {
                co_return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "Expired trash cleanup cursor did not advance"
                ));
            }
            after_id = page->next_after_id;
        }

        if (batch_iteration >= max_batches_per_run) {
            Logger::Info(log_context)
                << "[cleanup_batch] trash reached max batches per run cap: max="
                << max_batches_per_run << " rows_deleted=" << deleted_count;
        }
        Logger::Info(log_context) << "Trash cleanup completed: deleted_count=" << deleted_count;
        co_return deleted_count;
    }

    auto TrashService::CleanupExpiredTrashPage(
        uint64_t after_id,
        size_t limit,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<ExpiredTrashCleanupPageResult>> {
        constexpr size_t kMaxPageSize = 500;
        if (limit == 0 || limit > kMaxPageSize ||
            after_id == std::numeric_limits<uint64_t>::max()) {
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InvalidParameter,
                "Expired trash page bounds are invalid"
            ));
        }

        const auto started_at = std::chrono::steady_clock::now();
        try {
            auto trash_items = co_await m_trash_query.FetchExpiredLifecycleBatchAfterId(
                after_id,
                static_cast<int>(limit)
            );
            ExpiredTrashCleanupPageResult page{
                .candidates = trash_items.size(),
                .next_after_id = after_id,
                .has_more = trash_items.size() == limit,
            };
            if (trash_items.empty()) {
                co_return page;
            }
            page.next_after_id = trash_items.back().id;

            auto chunks = BatchUtils::Chunk(trash_items, DEFAULT_BATCH_CHUNK_SIZE);
            for (const auto& chunk : chunks) {
                if (chunk.empty()) {
                    continue;
                }

                auto delete_result = co_await PermanentlyDeleteTrashItems(
                    chunk,
                    false,
                    log_context
                );
                page.deleted += static_cast<size_t>(delete_result.deleted_count);
                if (delete_result.deleted_count != static_cast<int>(chunk.size())) {
                    co_return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Expired trash page contains an invalid content reference"
                    ));
                }
            }

            Logger::Info(log_context)
                << "[cleanup_batch] trash after_id=" << after_id
                << " candidates=" << page.candidates
                << " deleted=" << page.deleted
                << " next_after_id=" << page.next_after_id
                << " has_more=" << page.has_more
                << " batch_duration_ms="
                << std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - started_at
                   )
                       .count();
            co_return page;
        } catch (const drogon::orm::DrogonDbException& error) {
            Logger::Error(log_context)
                << "Database error cleaning expired trash page: " << error.base().what();
        } catch (const std::exception& error) {
            Logger::Error(log_context)
                << "Failed to clean expired trash page: " << error.what();
        }
        co_return std::unexpected(
            ErrorInfo(ErrorCode::InternalError, "Failed to clean expired trash")
        );
    }

    /// ==================== 公共方法实现 ====================

    auto TrashService::List(
        uint64_t user_id,
        int page,
        int page_size,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<std::vector<TrashItemResponse>>> {

        Logger::Info(log_context) << "Fetching trash list: user_id=" << user_id
                                  << ", page=" << page << ", page_size=" << page_size;

        try {
            auto offset = (page - 1) * page_size;
            auto records = co_await m_trash_query.FetchListPageForUser(user_id, page_size, offset);

            std::vector<TrashItemResponse> responses;
            responses.reserve(records.size());

            for (const auto& record : records) {
                TrashItemResponse response;
                response.id = record.id;
                response.type = record.item_type;
                response.original_id = record.item_id;
                response.name = record.item_name;
                response.size = record.item_size;
                response.original_path = record.original_path;
                response.deleted_at = record.deleted_at;
                response.expires_at = record.expires_at;
                responses.push_back(response);
            }

            Logger::Debug(log_context) << "Found " << responses.size() << " trash items";
            co_return responses;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error(log_context)
                << "Database error fetching trash list: user_id=" << user_id << " - "
                << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to fetch trash list, please try again later"
            ));
        } catch (const std::exception& e) {
            Logger::Error(log_context)
                << "Unknown error fetching trash list: user_id=" << user_id << " - " << e.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to fetch trash list, please try again later"
            ));
        }
    }

    auto TrashService::Count(uint64_t user_id, disk::utils::LogContext log_context)
        -> drogon::Task<Result<int>> {
        Logger::Debug(log_context) << "Counting trash items: user_id=" << user_id;

        try {
            auto count = co_await m_trash_query.CountForUser(user_id);
            co_return count;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error(log_context) << "Failed to count trash items: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to count trash items")
            );
        }
    }

    auto TrashService::Restore(
        uint64_t user_id,
        const std::vector<uint64_t>& trash_ids,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<BatchRestoreResponse>> {

        Logger::Info(log_context) << "Batch restoring trash items: user_id=" << user_id
                                  << ", count=" << trash_ids.size();

        BatchRestoreResponse response;
        response.summary.total = static_cast<int>(trash_ids.size());
        response.results.reserve(trash_ids.size());

        std::unordered_map<uint64_t, TrashLifecycleRecord> trash_items_by_id;
        trash_items_by_id.reserve(trash_ids.size());

        try {
            auto prefetched_items = co_await m_trash_query.PrefetchLifecycleRowsByIds(trash_ids);
            for (auto& item : prefetched_items) {
                trash_items_by_id[item.id] = std::move(item);
            }
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error(log_context)
                << "Failed to batch fetch trash items for restore: user_id=" << user_id << " - "
                << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to restore trash items, please try again later"
            ));
        }

        for (auto trash_id : trash_ids) {
            BatchResultItem result;
            result.trash_id = trash_id;

            auto item_it = trash_items_by_id.find(trash_id);
            if (item_it == trash_items_by_id.end()) {
                result.status = "failed";
                result.code = static_cast<uint16_t>(ErrorCode::ResourceNotFound);
                result.message = "Trash item not found";
                result.field = "trash_id";
                result.value = std::to_string(trash_id);
                response.summary.failure_count++;
                response.results.push_back(result);
                continue;
            }

            const auto& trash_item = item_it->second;
            if (trash_item.user_id != user_id) {
                result.status = "failed";
                result.code = static_cast<uint16_t>(ErrorCode::ResourceNotFound);
                result.message = "Trash item not found";
                result.field = "trash_id";
                result.value = std::to_string(trash_id);
                response.summary.failure_count++;
                response.results.push_back(result);
                continue;
            }

            if (trash_item.item_type == "file") {
                co_await RestoreFile(trash_item, user_id, result, log_context);
            } else if (trash_item.item_type == "folder") {
                co_await RestoreFolder(trash_item, user_id, result, log_context);
            } else {
                result.status = "failed";
                result.code = static_cast<uint16_t>(ErrorCode::ValidationFailed);
                result.message = "Unknown item type";
            }

            if (result.status == "success") {
                response.summary.success_count++;
            } else {
                response.summary.failure_count++;
            }
            response.results.push_back(result);
        }

        Logger::Info(log_context) << "Batch restore completed: total=" << response.summary.total
                                  << ", success=" << response.summary.success_count
                                  << ", failure=" << response.summary.failure_count;

        if (response.summary.success_count > 0) {
            co_await disk::file::FileListCache::Invalidate(
                m_redis_service,
                user_id,
                log_context
            );
        }

        co_return response;
    }

    auto TrashService::Delete(
        uint64_t user_id,
        const std::vector<uint64_t>& trash_ids,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<BatchDeleteResponse>> {

        Logger::Info(log_context)
            << "Batch permanently deleting trash items: user_id=" << user_id
            << ", count=" << trash_ids.size();

        BatchDeleteResponse response;
        response.summary.total = static_cast<int>(trash_ids.size());
        response.results.reserve(trash_ids.size());

        uint64_t total_freed_space = 0;

        std::unordered_map<uint64_t, TrashLifecycleRecord> trash_items_by_id;
        trash_items_by_id.reserve(trash_ids.size());

        try {
            auto prefetched_items = co_await m_trash_query.PrefetchLifecycleRowsByIds(trash_ids);
            for (auto& item : prefetched_items) {
                trash_items_by_id[item.id] = std::move(item);
            }
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error(log_context)
                << "Failed to batch fetch trash items for delete: user_id=" << user_id << " - "
                << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to delete trash items, please try again later"
            ));
        }

        for (auto trash_id : trash_ids) {
            BatchResultItem result;
            result.trash_id = trash_id;

            auto item_it = trash_items_by_id.find(trash_id);
            if (item_it == trash_items_by_id.end()) {
                result.status = "failed";
                result.code = static_cast<uint16_t>(ErrorCode::ResourceNotFound);
                result.message = "Trash item not found";
                result.field = "trash_id";
                result.value = std::to_string(trash_id);
                response.summary.failure_count++;
                response.results.push_back(result);
                continue;
            }

            const auto& trash_item = item_it->second;
            if (trash_item.user_id != user_id) {
                result.status = "failed";
                result.code = static_cast<uint16_t>(ErrorCode::ResourceNotFound);
                result.message = "Trash item not found";
                result.field = "trash_id";
                result.value = std::to_string(trash_id);
                response.summary.failure_count++;
                response.results.push_back(result);
                continue;
            }

            if (trash_item.item_type == "file") {
                auto content_id_result = disk::services::trash_content_internal::ResolveRequiredContentId(
                    trash_item.content_id,
                    trash_item.item_data
                );
                if (!content_id_result.has_value()) {
                    Logger::Warn(log_context)
                        << "Cannot permanently delete trash file without valid content_id: trash_id="
                        << trash_id;
                    result.status = "failed";
                    result.code = static_cast<uint16_t>(content_id_result.error().code);
                    result.message = content_id_result.error().message;
                    result.field = "content_id";
                    result.value = trash_item.content_id.has_value() ? std::to_string(trash_item.content_id.value()) : "NULL";
                    response.summary.failure_count++;
                    response.results.push_back(result);
                    continue;
                }
            } else if (trash_item.item_type != "folder") {
                result.status = "failed";
                result.code = static_cast<uint16_t>(ErrorCode::ValidationFailed);
                result.message = "Unknown item type";
                response.summary.failure_count++;
                response.results.push_back(result);
                continue;
            }

            try {
                auto delete_result = co_await PermanentlyDeleteTrashItems(
                    { trash_item },
                    true,
                    log_context
                );

                result.status = "success";
                result.freed_space = delete_result.freed_space;
                response.summary.success_count++;
                total_freed_space += delete_result.freed_space;

                Logger::Info(log_context)
                    << "Trash item permanently deleted: trash_id=" << trash_id
                    << ", freed_space=" << delete_result.freed_space;
            } catch (const std::exception& e) {
                Logger::Error(log_context)
                    << "Failed to permanently delete trash item: trash_id=" << trash_id << " - "
                    << e.what();
                result.status = "failed";
                result.code = static_cast<uint16_t>(ErrorCode::InternalError);
                result.message = trash_item.item_type == "folder" ? "Failed to permanently delete folder" : "Failed to permanently delete file";
                response.summary.failure_count++;
            }

            response.results.push_back(result);
        }

        Logger::Info(log_context) << "Batch delete completed: total=" << response.summary.total
                                  << ", success=" << response.summary.success_count
                                  << ", failure=" << response.summary.failure_count
                                  << ", freed_space=" << total_freed_space;

        co_return response;
    }

    auto TrashService::DeleteAll(uint64_t user_id, disk::utils::LogContext log_context)
        -> drogon::Task<Result<DeleteAllResponse>> {
        Logger::Info(log_context) << "Emptying trash: user_id=" << user_id;

        DeleteAllResponse response;
        response.deleted_count = 0;
        response.freed_space = 0;

        try {
            auto trash_items = co_await m_trash_query.FetchLifecycleRowsForUser(user_id);

            auto chunks = BatchUtils::Chunk(trash_items, DEFAULT_BATCH_CHUNK_SIZE);
            for (const auto& chunk : chunks) {
                if (chunk.empty()) {
                    continue;
                }

                try {
                    auto delete_result = co_await PermanentlyDeleteTrashItems(
                        chunk,
                        false,
                        log_context
                    );
                    response.deleted_count += delete_result.deleted_count;
                    response.freed_space += delete_result.freed_space;
                } catch (const std::exception& e) {
                    Logger::Error(log_context)
                        << "Failed to process DeleteAll chunk atomically: user_id=" << user_id
                        << " - " << e.what();
                    continue;
                }
            }

            Logger::Info(log_context) << "Trash emptied: user_id=" << user_id
                                      << ", deleted=" << response.deleted_count
                                      << ", freed_space=" << response.freed_space;

            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error(log_context) << "Database error emptying trash: user_id=" << user_id
                                       << " - " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to empty trash, please try again later")
            );
        } catch (const std::exception& e) {
            Logger::Error(log_context) << "Unknown error emptying trash: user_id=" << user_id
                                       << " - " << e.what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to empty trash, please try again later")
            );
        }
    }

    /// ==================== 私有方法实现 ====================

    auto TrashService::RestoreFile(
        const TrashLifecycleRecord& trash_item,
        uint64_t user_id,
        BatchResultItem& result,
        disk::utils::LogContext log_context
    ) -> drogon::Task<void> {
        uint64_t restored_file_id = 0;
        uint64_t resolved_content_id = 0;
        std::string restored_name;
        std::string restored_path;
        bool restored_to_root = false;
        bool resolved_legacy_content_id = false;
        bool invalid_content_id = false;
        std::string invalid_content_value;
        std::optional<ErrorInfo> restore_error;
        bool restored = false;

        disk::file::TransactionRunner transaction_runner(
            m_db_client,
            ErrorInfo(ErrorCode::InternalError, "Failed to restore file"),
            log_context
        );
        for (uint16_t candidate_index = 0; candidate_index <= 1000; ++candidate_index) {
            bool candidate_occupied = false;
            restored_to_root = false;
            resolved_legacy_content_id = false;

            auto transaction_result = co_await transaction_runner.Run(
                [&](const drogon::orm::DbClientPtr& transaction) -> drogon::Task<Result<void>> {
                    auto locked_trash_item = co_await m_trash_query.FetchLifecycleRowForUpdate(
                        transaction,
                        trash_item.id,
                        user_id
                    );
                    if (!locked_trash_item.has_value() || locked_trash_item->item_type != "file") {
                        co_return std::unexpected(
                            ErrorInfo(ErrorCode::ResourceNotFound, "Trash item not found")
                        );
                    }

                    auto content_id_result =
                        disk::services::trash_content_internal::ResolveRequiredContentId(
                            locked_trash_item->content_id,
                            locked_trash_item->item_data
                        );
                    if (!content_id_result.has_value()) {
                        invalid_content_id = true;
                        invalid_content_value = locked_trash_item->content_id.has_value()
                                                       ? std::to_string(*locked_trash_item->content_id)
                                                       : "NULL";
                        co_return std::unexpected(content_id_result.error());
                    }
                    resolved_content_id = content_id_result->value;
                    resolved_legacy_content_id =
                        content_id_result->source ==
                        disk::services::trash_content_internal::ContentIdSource::ItemData;

                    const auto base_name = ExtractBaseName(locked_trash_item->item_name);
                    const auto extension = ExtractExtension(locked_trash_item->item_name);
                    restored_name = locked_trash_item->item_name;
                    if (candidate_index > 0) {
                        restored_name =
                            base_name + " (" + std::to_string(candidate_index) + ")";
                        if (!extension.empty()) {
                            restored_name += "." + extension;
                        }
                    }

                    uint64_t target_folder_id = locked_trash_item->original_folder_id;
                    std::string parent_path = "/";
                    disk::folder::FolderRepository folder_repository;
                    if (target_folder_id > 0) {
                        auto parent_snapshot = co_await folder_repository.FindOwnedFolder(
                            transaction,
                            target_folder_id,
                            user_id
                        );
                        if (!parent_snapshot.has_value()) {
                            restored_to_root = true;
                            target_folder_id = 0;
                        }
                    }

                    disk::file::FileRepository file_repository;
                    co_await file_repository.AcquireNameLock(
                        transaction,
                        user_id,
                        target_folder_id,
                        restored_name
                    );

                    if (target_folder_id > 0) {
                        auto parent_folder = co_await folder_repository.FindOwnedFolderForUpdate(
                            transaction,
                            target_folder_id,
                            user_id
                        );
                        if (parent_folder.has_value()) {
                            parent_path = parent_folder->getValueOfPath();
                        } else {
                            restored_to_root = true;
                            target_folder_id = 0;
                            co_await file_repository.AcquireNameLock(
                                transaction,
                                user_id,
                                target_folder_id,
                                restored_name
                            );
                        }
                    }

                    if (co_await file_repository.NameExistsExcluding(
                            transaction,
                            restored_name,
                            target_folder_id,
                            user_id,
                            0
                        )) {
                        candidate_occupied = true;
                        co_return std::unexpected(
                            ErrorInfo(ErrorCode::ResourceConflict, "Restore filename occupied")
                        );
                    }

                    Json::Value item_data;
                    Json::Reader reader;
                    reader.parse(locked_trash_item->item_data, item_data);
                    restored_path = parent_path + restored_name;

                    Files file;
                    file.setUserId(user_id);
                    file.setContentId(resolved_content_id);
                    file.setFolderId(target_folder_id);
                    file.setName(restored_name);
                    file.setExtension(ExtractExtension(restored_name));
                    file.setSize(locked_trash_item->item_size);
                    file.setMimeType(
                        item_data.get("mime_type", "application/octet-stream").asString()
                    );
                    file.setPath(restored_path);
                    file.setIsFavorite(false);
                    file.setDownloadCount(0);
                    file.setCreatedAt(trantor::Date::now());
                    file.setUpdatedAt(trantor::Date::now());

                    CoroMapper<Files> file_mapper(transaction);
                    auto inserted_file = co_await file_mapper.insert(file);
                    restored_file_id = inserted_file.getValueOfId();

                    if (target_folder_id > 0) {
                        auto parent_updated = co_await folder_repository.ApplyItemCountDelta(
                            transaction,
                            target_folder_id,
                            user_id,
                            1,
                            trantor::Date::now()
                        );
                        if (!parent_updated) {
                            co_return std::unexpected(
                                ErrorInfo(ErrorCode::InternalError, "Failed to restore file")
                            );
                        }
                    }

                    auto delete_result = co_await transaction->execSqlCoro(
                        "DELETE FROM trash "
                        "WHERE id = $1 AND user_id = $2 AND item_type = 'file'",
                        locked_trash_item->id,
                        user_id
                    );
                    if (delete_result.affectedRows() != 1) {
                        co_return std::unexpected(
                            ErrorInfo(ErrorCode::InternalError, "Failed to restore file")
                        );
                    }

                    co_return {};
                }
            );

            if (transaction_result.has_value()) {
                restored = true;
                break;
            }
            if (candidate_occupied) {
                continue;
            }
            restore_error = transaction_result.error();
            break;
        }

        if (!restored) {
            if (!restore_error.has_value()) {
                restore_error = ErrorInfo(ErrorCode::InternalError, "Failed to restore file");
            }
            result.status = "failed";
            result.code = static_cast<uint16_t>(restore_error->code);
            result.message = restore_error->message;
            if (invalid_content_id) {
                result.field = "content_id";
                result.value = invalid_content_value;
                Logger::Warn(log_context)
                    << "Cannot restore trash file without valid content_id: trash_id="
                    << trash_item.id;
            } else {
                Logger::Error(log_context)
                    << "Failed to restore file: trash_id=" << trash_item.id;
            }
            co_return;
        }

        result.status = "success";
        result.file_id = restored_file_id;
        result.path = restored_path;

        if (restored_to_root) {
            Logger::Debug(log_context)
                << "Original folder not found, restored file to root: trash_id=" << trash_item.id;
        }
        if (resolved_legacy_content_id) {
            Logger::Debug(log_context)
                << "Resolved legacy trash content_id from item_data during restore: trash_id="
                << trash_item.id << ", content_id=" << resolved_content_id;
        }
        Logger::Info(log_context)
            << "File restored successfully: trash_id=" << trash_item.id
            << ", file_id=" << restored_file_id << ", name=" << restored_name
            << ", path=" << restored_path;
    }

    auto TrashService::RestoreFolder(
        const TrashLifecycleRecord& trash_item,
        uint64_t user_id,
        BatchResultItem& result,
        disk::utils::LogContext log_context
    ) -> drogon::Task<void> {
        uint64_t restored_folder_id = 0;
        size_t restored_folder_count = 0;
        size_t restored_file_count = 0;
        std::string restored_name;
        std::string restored_path;
        bool restored_to_root = false;
        std::optional<ErrorInfo> restore_error;
        bool restored = false;

        disk::file::TransactionRunner transaction_runner(
            m_db_client,
            ErrorInfo(ErrorCode::InternalError, "Failed to restore folder"),
            log_context
        );
        for (uint16_t candidate_index = 0; candidate_index <= 1000; ++candidate_index) {
            bool candidate_occupied = false;
            restored_to_root = false;

            auto transaction_result = co_await transaction_runner.Run(
                [&](const drogon::orm::DbClientPtr& transaction) -> drogon::Task<Result<void>> {
                    auto locked_trash_item = co_await m_trash_query.FetchLifecycleRowForUpdate(
                        transaction,
                        trash_item.id,
                        user_id
                    );
                    if (!locked_trash_item.has_value() || locked_trash_item->item_type != "folder") {
                        co_return std::unexpected(
                            ErrorInfo(ErrorCode::ResourceNotFound, "Trash item not found")
                        );
                    }

                    auto snapshot = ParseFolderTreeSnapshot(locked_trash_item->item_data);
                    restored_name = locked_trash_item->item_name;
                    if (candidate_index > 0) {
                        restored_name += " (" + std::to_string(candidate_index) + ")";
                    }

                    uint64_t target_parent_id = locked_trash_item->original_folder_id;
                    std::string parent_path = "/";
                    uint32_t parent_depth = 0;
                    disk::folder::FolderRepository folder_repository;
                    if (target_parent_id > 0) {
                        auto parent_snapshot = co_await folder_repository.FindOwnedFolder(
                            transaction,
                            target_parent_id,
                            user_id
                        );
                        if (!parent_snapshot.has_value()) {
                            restored_to_root = true;
                            target_parent_id = 0;
                        }
                    }

                    co_await folder_repository.AcquireNameLock(
                        transaction,
                        user_id,
                        target_parent_id,
                        restored_name
                    );

                    if (target_parent_id > 0) {
                        auto parent_folder = co_await folder_repository.FindOwnedFolderForUpdate(
                            transaction,
                            target_parent_id,
                            user_id
                        );
                        if (parent_folder.has_value()) {
                            parent_path = parent_folder->getValueOfPath();
                            parent_depth = parent_folder->getValueOfDepth();
                        } else {
                            restored_to_root = true;
                            target_parent_id = 0;
                            co_await folder_repository.AcquireNameLock(
                                transaction,
                                user_id,
                                target_parent_id,
                                restored_name
                            );
                        }
                    }

                    if (co_await folder_repository.NameExistsExcluding(
                            transaction,
                            restored_name,
                            target_parent_id,
                            user_id,
                            0
                        )) {
                        candidate_occupied = true;
                        co_return std::unexpected(
                            ErrorInfo(ErrorCode::ResourceConflict, "Restore folder name occupied")
                        );
                    }

                    restored_path = parent_path + restored_name + "/";
                    const auto folder_depth = parent_depth + 1;
                    const auto now = trantor::Date::now();
                    CoroMapper<Folders> folder_mapper(transaction);
                    CoroMapper<Files> file_mapper(transaction);

                    Folders root_folder;
                    root_folder.setUserId(user_id);
                    root_folder.setParentId(target_parent_id);
                    root_folder.setName(restored_name);
                    root_folder.setPath(restored_path);
                    root_folder.setDepth(folder_depth);
                    root_folder.setItemCount(snapshot.has_value() ? snapshot->root.item_count : 0);
                    root_folder.setCreatedAt(now);
                    root_folder.setUpdatedAt(now);

                    auto inserted_root = co_await folder_mapper.insert(root_folder);
                    restored_folder_id = inserted_root.getValueOfId();
                    restored_folder_count = 1;
                    restored_file_count = 0;

                    if (snapshot.has_value()) {
                        std::unordered_map<uint64_t, uint64_t> folder_id_map;
                        std::unordered_map<uint64_t, std::string> folder_path_map;
                        folder_id_map.reserve(snapshot->folders.size() + 1);
                        folder_path_map.reserve(snapshot->folders.size() + 1);
                        folder_id_map[snapshot->root.id] = restored_folder_id;
                        folder_path_map[snapshot->root.id] = restored_path;

                        auto remaining_folders = snapshot->folders;
                        while (!remaining_folders.empty()) {
                            bool progressed = false;
                            for (auto it = remaining_folders.begin(); it != remaining_folders.end();) {
                                auto parent_it = folder_id_map.find(it->parent_id);
                                auto parent_path_it = folder_path_map.find(it->parent_id);
                                if (parent_it == folder_id_map.end() || parent_path_it == folder_path_map.end()) {
                                    ++it;
                                    continue;
                                }

                                const auto child_path = parent_path_it->second + it->name + "/";
                                const auto depth_delta = it->depth > snapshot->root.depth ? it->depth - snapshot->root.depth : 1;

                                Folders folder;
                                folder.setUserId(user_id);
                                folder.setParentId(parent_it->second);
                                folder.setName(it->name);
                                folder.setPath(child_path);
                                folder.setDepth(folder_depth + depth_delta);
                                folder.setItemCount(it->item_count);
                                folder.setCreatedAt(now);
                                folder.setUpdatedAt(now);

                                auto inserted_folder = co_await folder_mapper.insert(folder);
                                folder_id_map[it->id] = inserted_folder.getValueOfId();
                                folder_path_map[it->id] = child_path;
                                it = remaining_folders.erase(it);
                                ++restored_folder_count;
                                progressed = true;
                            }

                            if (!progressed) {
                                co_return std::unexpected(
                                    ErrorInfo(ErrorCode::InternalError, "Failed to restore folder")
                                );
                            }
                        }

                        for (const auto& snapshot_file : snapshot->files) {
                            auto folder_it = folder_id_map.find(snapshot_file.folder_id);
                            auto path_it = folder_path_map.find(snapshot_file.folder_id);
                            if (folder_it == folder_id_map.end() || path_it == folder_path_map.end()) {
                                co_return std::unexpected(
                                    ErrorInfo(ErrorCode::InternalError, "Failed to restore folder")
                                );
                            }

                            Files file;
                            file.setUserId(user_id);
                            file.setContentId(snapshot_file.content_id);
                            file.setFolderId(folder_it->second);
                            file.setName(snapshot_file.name);
                            file.setExtension(snapshot_file.extension);
                            file.setSize(snapshot_file.size);
                            file.setMimeType(snapshot_file.mime_type);
                            file.setPath(path_it->second + snapshot_file.name);
                            file.setIsFavorite(snapshot_file.is_favorite);
                            file.setDownloadCount(snapshot_file.download_count);
                            file.setCreatedAt(now);
                            file.setUpdatedAt(now);
                            co_await file_mapper.insert(file);
                            ++restored_file_count;
                        }
                    }

                    if (target_parent_id > 0) {
                        auto parent_updated = co_await folder_repository.ApplyItemCountDelta(
                            transaction,
                            target_parent_id,
                            user_id,
                            1,
                            now
                        );
                        if (!parent_updated) {
                            co_return std::unexpected(
                                ErrorInfo(ErrorCode::InternalError, "Failed to restore folder")
                            );
                        }
                    }

                    auto delete_result = co_await transaction->execSqlCoro(
                        "DELETE FROM trash " "WHERE id = $1 AND user_id = $2 AND item_type = 'folder'",
                        locked_trash_item->id,
                        user_id
                    );
                    if (delete_result.affectedRows() != 1) {
                        co_return std::unexpected(
                            ErrorInfo(ErrorCode::InternalError, "Failed to restore folder")
                        );
                    }

                    co_return {};
                }
            );

            if (transaction_result.has_value()) {
                restored = true;
                break;
            }
            if (candidate_occupied) {
                continue;
            }
            restore_error = transaction_result.error();
            break;
        }

        if (!restored) {
            if (!restore_error.has_value()) {
                restore_error = ErrorInfo(ErrorCode::InternalError, "Failed to restore folder");
            }
            result.status = "failed";
            result.code = static_cast<uint16_t>(restore_error->code);
            result.message = restore_error->message;
            Logger::Error(log_context) << "Failed to restore folder: trash_id=" << trash_item.id;
            co_return;
        }

        result.status = "success";
        result.folder_id = restored_folder_id;
        result.path = restored_path;

        if (restored_to_root) {
            Logger::Debug(log_context)
                << "Original parent folder not found, restored folder to root: trash_id="
                << trash_item.id;
        }
        Logger::Info(log_context)
            << "Folder tree restored successfully: trash_id=" << trash_item.id
            << ", folder_id=" << restored_folder_id << ", name=" << restored_name
            << ", path=" << restored_path << ", folder_count=" << restored_folder_count
            << ", file_count=" << restored_file_count;
    }

    auto TrashService::DeleteFile(
        const TrashLifecycleRecord& trash_item,
        uint64_t user_id,
        BatchResultItem& result,
        disk::utils::LogContext log_context
    ) -> drogon::Task<uint64_t> {

        if (trash_item.user_id != user_id || trash_item.item_type != "file") {
            result.status = "failed";
            result.code = static_cast<uint16_t>(ErrorCode::ResourceNotFound);
            result.message = "Trash item not found";
            co_return 0;
        }

        auto content_id_result = disk::services::trash_content_internal::ResolveRequiredContentId(
            trash_item.content_id,
            trash_item.item_data
        );
        if (!content_id_result.has_value()) {
            Logger::Warn(log_context)
                << "Cannot permanently delete trash file without valid content_id: trash_id="
                << trash_item.id;
            result.status = "failed";
            result.code = static_cast<uint16_t>(content_id_result.error().code);
            result.message = content_id_result.error().message;
            result.field = "content_id";
            result.value = trash_item.content_id.has_value() ? std::to_string(trash_item.content_id.value()) : "NULL";
            co_return 0;
        }

        try {
            auto delete_result = co_await PermanentlyDeleteTrashItems(
                { trash_item },
                true,
                log_context
            );

            result.status = "success";
            result.freed_space = delete_result.freed_space;
            Logger::Info(log_context)
                << "File permanently deleted: trash_id=" << trash_item.id
                << ", freed_space=" << delete_result.freed_space;
            co_return delete_result.freed_space;
        } catch (const std::exception& e) {
            Logger::Error(log_context)
                << "Failed to permanently delete file: trash_id=" << trash_item.id << " - "
                << e.what();
            result.status = "failed";
            result.code = static_cast<uint16_t>(ErrorCode::InternalError);
            result.message = "Failed to permanently delete file";
            co_return 0;
        }
    }

    auto TrashService::DeleteFolder(
        const TrashLifecycleRecord& trash_item,
        uint64_t user_id,
        BatchResultItem& result,
        disk::utils::LogContext log_context
    ) -> drogon::Task<uint64_t> {

        if (trash_item.user_id != user_id || trash_item.item_type != "folder") {
            result.status = "failed";
            result.code = static_cast<uint16_t>(ErrorCode::ResourceNotFound);
            result.message = "Trash item not found";
            co_return 0;
        }

        try {
            auto delete_result = co_await PermanentlyDeleteTrashItems(
                { trash_item },
                true,
                log_context
            );

            auto snapshot_content_ids = ExtractSnapshotContentIds(trash_item.item_data);
            result.status = "success";
            result.freed_space = delete_result.freed_space;

            Logger::Info(log_context)
                << "Folder permanently deleted: trash_id=" << trash_item.id
                << ", freed_space=" << delete_result.freed_space
                << ", snapshot_file_count=" << snapshot_content_ids.size();

            co_return delete_result.freed_space;
        } catch (const std::exception& e) {
            Logger::Error(log_context)
                << "Failed to permanently delete folder: trash_id=" << trash_item.id << " - "
                << e.what();
            result.status = "failed";
            result.code = static_cast<uint16_t>(ErrorCode::InternalError);
            result.message = "Failed to permanently delete folder";
            co_return 0;
        }
    }

    auto TrashService::PermanentlyDeleteTrashItems(
        const std::vector<TrashLifecycleRecord>& trash_items,
        bool require_valid_file_content,
        disk::utils::LogContext log_context
    ) -> drogon::Task<PermanentDeleteResult> {
        PermanentDeleteResult result;
        if (trash_items.empty()) {
            co_return result;
        }

        std::shared_ptr<drogon::orm::Transaction> transaction;
        try {
            transaction = co_await disk::file::TransactionRunner::Begin(m_db_client);

            std::vector<uint64_t> trash_ids;
            trash_ids.reserve(trash_items.size());
            std::unordered_map<uint64_t, uint64_t> content_ref_decrements;
            std::unordered_map<uint64_t, int64_t> user_storage_delta;

            for (const auto& item : trash_items) {
                if (item.item_type == "file") {
                    auto content_id_result = disk::services::trash_content_internal::ResolveRequiredContentId(
                        item.content_id,
                        item.item_data
                    );
                    if (!content_id_result.has_value()) {
                        if (require_valid_file_content) {
                            throw std::runtime_error(content_id_result.error().message);
                        }
                        Logger::Warn(log_context)
                            << "Skip permanent delete for legacy trash file without valid content_id: trash_id="
                            << item.id << ", user_id=" << item.user_id;
                        continue;
                    }

                    if (content_id_result->source ==
                        disk::services::trash_content_internal::ContentIdSource::ItemData) {
                        Logger::Debug(log_context)
                            << "Resolved legacy trash content_id from item_data during permanent delete: trash_id="
                            << item.id << ", content_id=" << content_id_result->value;
                    }

                    content_ref_decrements[content_id_result->value] += 1;
                } else if (item.item_type == "folder") {
                    auto snapshot_content_ids = ExtractSnapshotContentIds(item.item_data);
                    for (const auto content_id : snapshot_content_ids) {
                        content_ref_decrements[content_id] += 1;
                    }
                } else {
                    throw std::runtime_error("Unknown item type in trash chunk");
                }

                trash_ids.push_back(item.id);
                user_storage_delta[item.user_id] -= static_cast<int64_t>(item.item_size);
                result.freed_space += item.item_size;
            }

            if (trash_ids.empty()) {
                transaction->rollback();
                co_return result;
            }

            if (!content_ref_decrements.empty()) {
                disk::content::ContentService content_service(m_db_client);
                auto decrement_result = co_await content_service.DecrementRefCountsAndEnqueueGc(
                    transaction,
                    content_ref_decrements,
                    log_context
                );
                if (!decrement_result) {
                    throw std::runtime_error(decrement_result.error().message);
                }
            }

            auto delete_result = co_await transaction->execSqlCoro(
                "DELETE FROM trash WHERE id IN (" + BatchUtils::BuildSafeNumericInClause(trash_ids) + ")"
            );
            if (delete_result.affectedRows() != trash_ids.size()) {
                throw std::runtime_error("Chunk delete affected rows mismatch");
            }

            disk::quota::QuotaService quota_service(m_db_client);
            for (const auto& [user_id, delta] : user_storage_delta) {
                if (delta != 0) {
                    auto quota_result = co_await quota_service.AdjustUsedStorageChecked(
                        transaction,
                        user_id,
                        delta,
                        log_context
                    );
                    if (!quota_result) {
                        throw std::runtime_error(quota_result.error().message);
                    }
                }
            }

            result.deleted_count = static_cast<int>(trash_ids.size());
            auto commit_result = co_await disk::file::TransactionRunner::Commit(
                transaction,
                log_context
            );
            if (!commit_result) {
                throw std::runtime_error("Trash permanent-delete transaction commit failed");
            }
            co_return result;
        } catch (...) {
            if (transaction) {
                try {
                    transaction->rollback();
                } catch (const std::exception& rollback_error) {
                    Logger::Error(log_context)
                        << "Trash permanent-delete rollback failed: " << rollback_error.what();
                }
            }
            throw;
        }
    }

    auto TrashService::ExtractExtension(const std::string& filename) -> std::string {
        auto pos = filename.rfind('.');
        if (pos == std::string::npos || pos == 0 || pos == filename.length() - 1) {
            return "";
        }

        auto paren_pos = filename.rfind(" (");
        if (paren_pos != std::string::npos && paren_pos < pos) {
            return "";
        }

        return filename.substr(pos + 1);
    }

    auto TrashService::ExtractBaseName(const std::string& filename) -> std::string {
        auto pos = filename.rfind('.');
        if (pos == std::string::npos || pos == 0) {
            return filename;
        }

        auto paren_pos = filename.rfind(" (");
        if (paren_pos != std::string::npos && paren_pos < pos) {
            return filename;
        }

        return filename.substr(0, pos);
    }

} // namespace disk::trash
