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
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include <drogon/orm/CoroMapper.h>
#include <drogon/orm/Criteria.h>
#include <json/writer.h>

#include "models/FileContents.hpp"
#include "models/Files.hpp"
#include "models/Folders.hpp"
#include "models/Trash.hpp"
#include "models/Users.hpp"
#include "services/ContentService.hpp"
#include "services/QuotaService.hpp"
#include "services/TransactionRunner.hpp"
#include "services/TrashContentIdResolver.hpp"
#include "storage/IFileStorage.hpp"
#include "storage/StorageMgr.hpp"
#include "utils/BatchUtils.hpp"
#include "utils/RedisKeyPrefix.hpp"

namespace disk::trash {

    using disk::utils::BatchUtils;
    using disk::utils::DEFAULT_BATCH_CHUNK_SIZE;
    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::FileContents;
    using drogon_model::disk::Files;
    using drogon_model::disk::Folders;
    using drogon_model::disk::Trash;
    using drogon_model::disk::Users;

    constexpr size_t MAX_PARALLEL_DELETE_PATHS = 4;

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

    [[nodiscard]]
    auto ParallelDeletePaths(
        disk::storage::BlobStore* blob_store,
        const std::vector<std::filesystem::path>& paths,
        size_t max_concurrent = MAX_PARALLEL_DELETE_PATHS
    ) -> drogon::Task<std::vector<Result<void>>> {
        std::vector<Result<void>> results;
        results.reserve(paths.size());
        (void)max_concurrent;

        for (const auto& path : paths) {
            results.push_back(co_await blob_store->DeletePath(path));
        }

        co_return results;
    }

    TrashService::TrashService(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)), m_trash_query(m_db_client) {
        Logger::Debug() << "TrashService initialization completed";
    }

    auto TrashService::CreateTrashRecords(
        const drogon::orm::DbClientPtr& client,
        const std::vector<disk::file::utils::TrashInsertItem>& trash_items,
        uint64_t user_id
    ) const -> drogon::Task<bool> {
        co_return co_await disk::file::utils::InsertTrashRecords(client, trash_items, user_id);
    }

    auto TrashService::MoveToTrash(MoveToTrashRequest request, uint64_t user_id)
        -> drogon::Task<Result<MoveToTrashResult>> {

        auto normalize_ids = [](std::vector<uint64_t> ids) {
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            return ids;
        };

        auto requested_file_ids = normalize_ids(std::move(request.file_ids));
        auto requested_folder_ids = normalize_ids(std::move(request.folder_ids));

        std::unordered_map<uint64_t, disk::file::utils::FolderDeletePlan> folder_plans =
            co_await disk::file::utils::FetchBatchFolderDeletePlans(m_db_client, requested_folder_ids, user_id);

        auto top_level_folder_ids = disk::file::utils::FilterCoveredFolderIds(requested_folder_ids, folder_plans);
        auto covered_file_ids = disk::file::utils::CollectCoveredFileIds(top_level_folder_ids, folder_plans);

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
                    "SELECT id, user_id, folder_id, content_id, name, extension, size, mime_type, path, " "is_favorite, download_count, last_accessed_at, created_at, updated_at " "FROM files WHERE id IN (" + BatchUtils::BuildSafeNumericInClause(chunk) + ") AND user_id = $1",
                    user_id
                );

                for (const auto& row : result) {
                    auto file = Files(row, -1);
                    file_map[file.getValueOfId()] = std::move(file);
                }
            } catch (const drogon::orm::DrogonDbException& e) {
                Logger::Warn() << "File batch fetch failed in move-to-trash, skipping chunk: " << e.base().what();
            }
        }

        std::vector<disk::file::utils::TrashInsertItem> trash_items;
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
                .item_data = disk::file::utils::BuildFolderSnapshot(plan),
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

        std::vector<uint64_t> affected_folder_ids;
        for (const auto& trash_item : trash_items) {
            if (trash_item.original_folder_id != 0) {
                affected_folder_ids.push_back(trash_item.original_folder_id);
            }
        }
        affected_folder_ids = normalize_ids(std::move(affected_folder_ids));

        disk::file::TransactionRunner transaction_runner(m_db_client);
        auto tx_result = co_await transaction_runner.Run(
            [&](const drogon::orm::DbClientPtr& transaction) -> drogon::Task<Result<void>> {
                auto insert_ok = co_await CreateTrashRecords(transaction, trash_items, user_id);
                if (!insert_ok) {
                    co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Failed to delete items"));
                }

                auto share_stats = co_await CleanupShareLinksForMovedItems(
                    transaction,
                    file_ids_to_delete,
                    folder_ids_to_delete
                );
                Logger::Debug() << "Cleaned share links during delete: file_links="
                                << share_stats.deleted_file_share_links
                                << ", folder_links=" << share_stats.deleted_folder_share_links
                                << ", cancelled_empty_shares=" << share_stats.cancelled_empty_shares;

                auto deleted_file_rows = co_await disk::file::utils::DeleteFilesByIds(transaction, file_ids_to_delete);
                if (deleted_file_rows != static_cast<int>(file_ids_to_delete.size())) {
                    co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Failed to delete items"));
                }

                auto deleted_folder_rows = co_await disk::file::utils::DeleteFoldersByIds(transaction, folder_ids_to_delete);
                if (deleted_folder_rows != static_cast<int>(folder_ids_to_delete.size())) {
                    co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Failed to delete items"));
                }

                co_return {};
            }
        );
        if (!tx_result) {
            Logger::Error() << "Delete transaction failed: " << tx_result.error().message;
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Failed to delete items"));
        }

        MoveToTrashResult result;
        result.deleted_file_count = deleted_file_count;
        result.deleted_folder_count = deleted_folder_count;
        result.deleted_count = deleted_file_count + deleted_folder_count;
        result.removed_file_ids = std::move(file_ids_to_delete);
        result.removed_folder_ids = std::move(folder_ids_to_delete);

        if (!affected_folder_ids.empty()) {
            co_await InvalidateFileListCache(user_id, affected_folder_ids);
        }

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

    auto TrashService::InvalidateFileListCache(uint64_t user_id, const std::vector<uint64_t>& folder_ids)
        -> drogon::Task<void> {
        for (const auto folder_id : folder_ids) {
            const auto prefix = disk::redis::RedisKeyPrefix::BuildFileListCachePrefix(user_id, folder_id);
            auto delete_result = co_await m_redis_service->DeleteByPrefix(prefix);
            if (!delete_result) {
                Logger::Warn() << "Failed to invalidate file list cache by prefix: " << prefix;
            }
        }
    }

    auto TrashService::CleanupExpiredTrashItems(
        int fetch_batch_size,
        int max_batches_per_run
    ) -> drogon::Task<Result<int>> {
        Logger::Info() << "Starting cleanup of expired trash items";

        try {
            int deleted_count = 0;
            uint64_t last_seen_id = 0;
            int batch_iteration = 0;

            while (batch_iteration < max_batches_per_run) {
                auto batch_start = std::chrono::steady_clock::now();
                auto trash_items = co_await m_trash_query.FetchExpiredLifecycleBatchAfterId(
                    last_seen_id,
                    fetch_batch_size
                );

                if (trash_items.empty()) {
                    break;
                }

                uint64_t batch_max_id = 0;
                for (const auto& item : trash_items) {
                    if (item.id > batch_max_id) {
                        batch_max_id = item.id;
                    }
                }

                int chunks_succeeded = 0;
                int chunks_failed = 0;
                int blobs_verified = 0;
                int blobs_deleted = 0;

                auto chunks = BatchUtils::Chunk(trash_items, DEFAULT_BATCH_CHUNK_SIZE);
                for (const auto& chunk : chunks) {
                    if (chunk.empty()) {
                        continue;
                    }

                    try {
                        auto delete_result = co_await PermanentlyDeleteTrashItems(chunk, false);
                        deleted_count += delete_result.deleted_count;
                        chunks_succeeded++;

                        if (!delete_result.zero_ref_content_ids.empty()) {
                            auto blob_stats = co_await CleanupVerifiedZeroRefBlobs(
                                delete_result.zero_ref_content_ids,
                                "expired-trash"
                            );
                            blobs_verified += blob_stats.verified_count;
                            blobs_deleted += blob_stats.deleted_count;
                        }
                    } catch (const std::exception& e) {
                        Logger::Error() << "Failed to cleanup expired trash chunk atomically: " << e.what();
                        chunks_failed++;
                        continue;
                    }
                }

                Logger::Info() << "[cleanup_batch] trash batch_iteration="
                               << batch_iteration
                               << " fetch_size=" << trash_items.size()
                               << " rows_deleted_so_far=" << deleted_count
                               << " chunks_succeeded=" << chunks_succeeded
                               << " chunks_failed=" << chunks_failed
                               << " blobs_verified=" << blobs_verified
                               << " blobs_deleted=" << blobs_deleted
                               << " batch_duration_ms="
                               << std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::steady_clock::now() - batch_start
                                  )
                                      .count();

                last_seen_id = batch_max_id;
                batch_iteration++;

                if (trash_items.size() < static_cast<size_t>(fetch_batch_size)) {
                    break;
                }
            }

            if (batch_iteration >= max_batches_per_run) {
                Logger::Info() << "[cleanup_batch] trash reached max batches per run cap: max="
                               << max_batches_per_run << " rows_deleted=" << deleted_count;
            }

            Logger::Info() << "Trash cleanup completed: deleted_count=" << deleted_count;
            co_return deleted_count;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Database error cleaning expired trash: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to clean expired trash")
            );
        } catch (const std::exception& e) {
            Logger::Error() << "Unknown error cleaning expired trash: " << e.what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to clean expired trash")
            );
        }
    }

    /// ==================== 公共方法实现 ====================

    auto TrashService::List(uint64_t user_id, int page, int page_size)
        -> drogon::Task<Result<std::vector<TrashItemResponse>>> {

        Logger::Info() << "Fetching trash list: user_id=" << user_id << ", page=" << page
                       << ", page_size=" << page_size;

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

            Logger::Debug() << "Found " << responses.size() << " trash items";
            co_return responses;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Database error fetching trash list: user_id=" << user_id << " - "
                            << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to fetch trash list, please try again later"
            ));
        } catch (const std::exception& e) {
            Logger::Error() << "Unknown error fetching trash list: user_id=" << user_id << " - "
                            << e.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to fetch trash list, please try again later"
            ));
        }
    }

    auto TrashService::Count(uint64_t user_id) -> drogon::Task<Result<int>> {
        Logger::Debug() << "Counting trash items: user_id=" << user_id;

        try {
            auto count = co_await m_trash_query.CountForUser(user_id);
            co_return count;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to count trash items: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to count trash items")
            );
        }
    }

    auto TrashService::Restore(uint64_t user_id, const std::vector<uint64_t>& trash_ids)
        -> drogon::Task<Result<BatchRestoreResponse>> {

        Logger::Info() << "Batch restoring trash items: user_id=" << user_id
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
            Logger::Error() << "Failed to batch fetch trash items for restore: user_id=" << user_id
                            << " - " << e.base().what();
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
                co_await RestoreFile(trash_item, user_id, result);
            } else if (trash_item.item_type == "folder") {
                co_await RestoreFolder(trash_item, user_id, result);
            } else {
                result.status = "failed";
                result.code = static_cast<uint16_t>(ErrorCode::ValidationFailed);
                result.message = "Unknown item type";
                response.summary.failure_count++;
            }

            if (result.status == "success") {
                response.summary.success_count++;
            }
            response.results.push_back(result);
        }

        Logger::Info() << "Batch restore completed: total=" << response.summary.total
                       << ", success=" << response.summary.success_count
                       << ", failure=" << response.summary.failure_count;

        co_return response;
    }

    auto TrashService::Delete(uint64_t user_id, const std::vector<uint64_t>& trash_ids)
        -> drogon::Task<Result<BatchDeleteResponse>> {

        Logger::Info() << "Batch permanently deleting trash items: user_id=" << user_id
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
            Logger::Error() << "Failed to batch fetch trash items for delete: user_id=" << user_id
                            << " - " << e.base().what();
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
                    Logger::Warn() << "Cannot permanently delete trash file without valid content_id: trash_id="
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
                auto delete_result = co_await PermanentlyDeleteTrashItems({ trash_item }, true);
                auto blob_stats = co_await CleanupVerifiedZeroRefBlobs(
                    delete_result.zero_ref_content_ids,
                    "manual-trash-delete"
                );
                (void)blob_stats;

                result.status = "success";
                result.freed_space = delete_result.freed_space;
                response.summary.success_count++;
                total_freed_space += delete_result.freed_space;

                Logger::Info() << "Trash item permanently deleted: trash_id=" << trash_id
                               << ", freed_space=" << delete_result.freed_space;
            } catch (const std::exception& e) {
                Logger::Error() << "Failed to permanently delete trash item: trash_id=" << trash_id
                                << " - " << e.what();
                result.status = "failed";
                result.code = static_cast<uint16_t>(ErrorCode::InternalError);
                result.message = trash_item.item_type == "folder" ? "Failed to permanently delete folder" : "Failed to permanently delete file";
                response.summary.failure_count++;
            }

            response.results.push_back(result);
        }

        Logger::Info() << "Batch delete completed: total=" << response.summary.total
                       << ", success=" << response.summary.success_count
                       << ", failure=" << response.summary.failure_count
                       << ", freed_space=" << total_freed_space;

        co_return response;
    }

    auto TrashService::DeleteAll(uint64_t user_id) -> drogon::Task<Result<DeleteAllResponse>> {
        Logger::Info() << "Emptying trash: user_id=" << user_id;

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
                    auto delete_result = co_await PermanentlyDeleteTrashItems(chunk, false);
                    response.deleted_count += delete_result.deleted_count;
                    response.freed_space += delete_result.freed_space;

                    auto blob_stats = co_await CleanupVerifiedZeroRefBlobs(
                        delete_result.zero_ref_content_ids,
                        "empty-trash"
                    );
                    (void)blob_stats;
                } catch (const std::exception& e) {
                    Logger::Error() << "Failed to process DeleteAll chunk atomically: user_id="
                                    << user_id << " - " << e.what();
                    continue;
                }
            }

            Logger::Info() << "Trash emptied: user_id=" << user_id
                           << ", deleted=" << response.deleted_count
                           << ", freed_space=" << response.freed_space;

            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Database error emptying trash: user_id=" << user_id << " - "
                            << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to empty trash, please try again later")
            );
        } catch (const std::exception& e) {
            Logger::Error() << "Unknown error emptying trash: user_id=" << user_id << " - " << e.what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to empty trash, please try again later")
            );
        }
    }

    /// ==================== 私有方法实现 ====================

    auto TrashService::RestoreFile(uint64_t trash_id, uint64_t user_id, BatchResultItem& result)
        -> drogon::Task<void> {

        try {
            CoroMapper<Trash> trash_mapper(m_db_client);
            auto trash_model = co_await trash_mapper.findOne(
                Criteria(Trash::Cols::_id, CompareOperator::EQ, trash_id)
            );

            TrashLifecycleRecord trash_item;
            trash_item.id = trash_model.getValueOfId();
            trash_item.user_id = trash_model.getValueOfUserId();
            trash_item.item_type = trash_model.getValueOfItemType();
            trash_item.item_id = trash_model.getValueOfItemId();
            trash_item.item_name = trash_model.getValueOfItemName();
            trash_item.item_size = trash_model.getValueOfItemSize();
            trash_item.original_folder_id = trash_model.getValueOfOriginalFolderId();
            trash_item.original_path = trash_model.getValueOfOriginalPath();
            trash_item.item_data =
                trash_model.getItemData() ? *trash_model.getItemData() : "";
            if (trash_model.getContentId()) {
                trash_item.content_id = *trash_model.getContentId();
            }

            co_await RestoreFile(trash_item, user_id, result);
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to restore file: trash_id=" << trash_id << " - "
                            << e.base().what();
            result.status = "failed";
            result.code = static_cast<uint16_t>(ErrorCode::InternalError);
            result.message = "Failed to restore file";
        }
    }

    auto TrashService::RestoreFile(
        const TrashLifecycleRecord& trash_item,
        uint64_t user_id,
        BatchResultItem& result
    ) -> drogon::Task<void> {

        try {
            auto trash_id = trash_item.id;
            auto original_folder_id = trash_item.original_folder_id;
            auto item_name = trash_item.item_name;

            auto target_folder_id = original_folder_id;
            std::string parent_path = "/";

            if (!co_await IsFolderExists(original_folder_id, user_id)) {
                Logger::Debug() << "Original folder not found, restoring to root: original_folder_id="
                                << original_folder_id;
                target_folder_id = 0;
            }

            if (target_folder_id > 0) {
                CoroMapper<Folders> folder_mapper(m_db_client);
                try {
                    auto parent_folder = co_await folder_mapper.findOne(
                        Criteria(Folders::Cols::_id, CompareOperator::EQ, target_folder_id)
                    );
                    parent_path = parent_folder.getValueOfPath();
                } catch (const drogon::orm::DrogonDbException& e) {
                    Logger::Warn() << "Failed to get parent folder path, using root: folder_id="
                                   << target_folder_id;
                    target_folder_id = 0;
                    parent_path = "/";
                }
            }

            auto final_name = item_name;
            if (co_await IsFilenameExists(target_folder_id, item_name, user_id)) {
                final_name =
                    co_await GenerateUniqueFilename(target_folder_id, item_name, user_id, true);
                Logger::Debug() << "Filename conflict, auto-renamed: " << item_name << " -> "
                                << final_name;
            }

            auto content_id_result =
                disk::services::trash_content_internal::ResolveRequiredContentId(
                    trash_item.content_id,
                    trash_item.item_data
                );
            if (!content_id_result.has_value()) {
                Logger::Warn() << "Cannot restore trash file without valid content_id: trash_id=" << trash_id;
                result.status = "failed";
                result.code = static_cast<uint16_t>(content_id_result.error().code);
                result.message = content_id_result.error().message;
                result.field = "content_id";
                result.value = trash_item.content_id.has_value() ? std::to_string(trash_item.content_id.value()) : "NULL";
                co_return;
            }

            if (content_id_result->source ==
                disk::services::trash_content_internal::ContentIdSource::ItemData) {
                Logger::Debug() << "Resolved legacy trash content_id from item_data during restore: trash_id="
                                << trash_id << ", content_id=" << content_id_result->value;
            }

            Json::Value item_data;
            Json::Reader reader;
            reader.parse(trash_item.item_data, item_data);

            std::string file_path = parent_path + final_name;

            Files file;
            file.setUserId(user_id);
            file.setContentId(content_id_result->value);
            file.setFolderId(target_folder_id);
            file.setName(final_name);
            file.setExtension(ExtractExtension(final_name));
            file.setSize(trash_item.item_size);
            file.setMimeType(item_data.get("mime_type", "application/octet-stream").asString());
            file.setPath(file_path);
            file.setIsFavorite(false);
            file.setDownloadCount(0);
            file.setCreatedAt(trantor::Date::now());
            file.setUpdatedAt(trantor::Date::now());

            CoroMapper<Files> file_mapper(m_db_client);
            auto inserted_file = co_await file_mapper.insert(file);

            CoroMapper<Trash> trash_mapper(m_db_client);
            co_await trash_mapper.deleteByPrimaryKey(trash_id);

            result.status = "success";
            result.file_id = inserted_file.getValueOfId();
            result.path = file_path;

            Logger::Info() << "File restored successfully: trash_id=" << trash_id
                           << ", file_id=" << inserted_file.getValueOfId() << ", name=" << final_name
                           << ", path=" << file_path;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to restore file: trash_id=" << trash_item.id << " - "
                            << e.base().what();
            result.status = "failed";
            result.code = static_cast<uint16_t>(ErrorCode::InternalError);
            result.message = "Failed to restore file";
        }
    }

    auto TrashService::RestoreFolder(uint64_t trash_id, uint64_t user_id, BatchResultItem& result)
        -> drogon::Task<void> {

        try {
            CoroMapper<Trash> trash_mapper(m_db_client);
            auto trash_model = co_await trash_mapper.findOne(
                Criteria(Trash::Cols::_id, CompareOperator::EQ, trash_id)
            );

            TrashLifecycleRecord trash_item;
            trash_item.id = trash_model.getValueOfId();
            trash_item.user_id = trash_model.getValueOfUserId();
            trash_item.item_type = trash_model.getValueOfItemType();
            trash_item.item_id = trash_model.getValueOfItemId();
            trash_item.item_name = trash_model.getValueOfItemName();
            trash_item.item_size = trash_model.getValueOfItemSize();
            trash_item.original_folder_id = trash_model.getValueOfOriginalFolderId();
            trash_item.original_path = trash_model.getValueOfOriginalPath();
            trash_item.item_data =
                trash_model.getItemData() ? *trash_model.getItemData() : "";
            if (trash_model.getContentId()) {
                trash_item.content_id = *trash_model.getContentId();
            }

            co_await RestoreFolder(trash_item, user_id, result);
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to restore folder: trash_id=" << trash_id << " - "
                            << e.base().what();
            result.status = "failed";
            result.code = static_cast<uint16_t>(ErrorCode::InternalError);
            result.message = "Failed to restore folder";
        }
    }

    auto TrashService::RestoreFolder(
        const TrashLifecycleRecord& trash_item,
        uint64_t user_id,
        BatchResultItem& result
    ) -> drogon::Task<void> {

        try {
            auto trash_id = trash_item.id;
            auto original_folder_id = trash_item.original_folder_id;
            auto item_name = trash_item.item_name;

            auto target_parent_id = original_folder_id;
            std::string parent_path = "/";
            uint32_t parent_depth = 0;

            if (!co_await IsFolderExists(original_folder_id, user_id)) {
                LOG_DEBUG
                    << "Original parent folder not found, restoring to root: original_folder_id="
                    << original_folder_id;
                target_parent_id = 0;
            }

            if (target_parent_id > 0) {
                CoroMapper<Folders> folder_mapper(m_db_client);
                try {
                    auto parent_folder = co_await folder_mapper.findOne(
                        Criteria(Folders::Cols::_id, CompareOperator::EQ, target_parent_id)
                    );
                    parent_path = parent_folder.getValueOfPath();
                    parent_depth = parent_folder.getValueOfDepth();
                } catch (const drogon::orm::DrogonDbException& e) {
                    Logger::Warn() << "Failed to get parent folder info, using root: folder_id="
                                   << target_parent_id;
                    target_parent_id = 0;
                    parent_path = "/";
                    parent_depth = 0;
                }
            }

            auto final_name = item_name;
            if (co_await IsFolderNameExists(target_parent_id, item_name, user_id)) {
                final_name =
                    co_await GenerateUniqueFilename(target_parent_id, item_name, user_id, false);
                Logger::Debug() << "Folder name conflict, auto-renamed: " << item_name << " -> "
                                << final_name;
            }

            std::string folder_path = parent_path + final_name + "/";
            uint32_t folder_depth = parent_depth + 1;

            CoroMapper<Folders> folder_mapper(m_db_client);
            CoroMapper<Files> file_mapper(m_db_client);
            CoroMapper<Trash> trash_mapper(m_db_client);

            auto snapshot = ParseFolderTreeSnapshot(trash_item.item_data);
            if (!snapshot.has_value()) {
                Folders folder;
                folder.setUserId(user_id);
                folder.setParentId(target_parent_id);
                folder.setName(final_name);
                folder.setPath(folder_path);
                folder.setDepth(folder_depth);
                folder.setItemCount(0);
                folder.setCreatedAt(trantor::Date::now());
                folder.setUpdatedAt(trantor::Date::now());

                auto inserted_folder = co_await folder_mapper.insert(folder);
                co_await trash_mapper.deleteByPrimaryKey(trash_id);

                result.status = "success";
                result.folder_id = inserted_folder.getValueOfId();
                result.path = folder_path;

                Logger::Info() << "Folder restored successfully: trash_id=" << trash_id
                               << ", folder_id=" << inserted_folder.getValueOfId() << ", name=" << final_name
                               << ", path=" << folder_path << ", depth=" << folder_depth;
                co_return;
            }

            std::unordered_map<uint64_t, uint64_t> folder_id_map;
            std::unordered_map<uint64_t, std::string> folder_path_map;
            folder_id_map.reserve(snapshot->folders.size() + 1);
            folder_path_map.reserve(snapshot->folders.size() + 1);

            Folders root_folder;
            root_folder.setUserId(user_id);
            root_folder.setParentId(target_parent_id);
            root_folder.setName(final_name);
            root_folder.setPath(folder_path);
            root_folder.setDepth(folder_depth);
            root_folder.setItemCount(snapshot->root.item_count);
            root_folder.setCreatedAt(trantor::Date::now());
            root_folder.setUpdatedAt(trantor::Date::now());

            auto inserted_root = co_await folder_mapper.insert(root_folder);
            auto root_new_id = inserted_root.getValueOfId();
            folder_id_map[snapshot->root.id] = root_new_id;
            folder_path_map[snapshot->root.id] = folder_path;

            auto remaining_folders = snapshot->folders;
            while (!remaining_folders.empty()) {
                bool progressed = false;
                for (auto it = remaining_folders.begin(); it != remaining_folders.end();) {
                    auto parent_it = folder_id_map.find(it->parent_id);
                    if (parent_it == folder_id_map.end()) {
                        ++it;
                        continue;
                    }

                    auto parent_path_it = folder_path_map.find(it->parent_id);
                    auto restored_path = parent_path_it->second + it->name + "/";
                    auto depth_delta = it->depth > snapshot->root.depth ? it->depth - snapshot->root.depth : 1;

                    Folders folder;
                    folder.setUserId(user_id);
                    folder.setParentId(parent_it->second);
                    folder.setName(it->name);
                    folder.setPath(restored_path);
                    folder.setDepth(folder_depth + depth_delta);
                    folder.setItemCount(it->item_count);
                    folder.setCreatedAt(trantor::Date::now());
                    folder.setUpdatedAt(trantor::Date::now());

                    auto inserted_folder = co_await folder_mapper.insert(folder);
                    folder_id_map[it->id] = inserted_folder.getValueOfId();
                    folder_path_map[it->id] = restored_path;
                    it = remaining_folders.erase(it);
                    progressed = true;
                }

                if (!progressed) {
                    throw std::runtime_error("Folder snapshot contains orphaned folder nodes");
                }
            }

            for (const auto& snapshot_file : snapshot->files) {
                auto folder_it = folder_id_map.find(snapshot_file.folder_id);
                auto path_it = folder_path_map.find(snapshot_file.folder_id);
                if (folder_it == folder_id_map.end() || path_it == folder_path_map.end()) {
                    throw std::runtime_error("Folder snapshot contains orphaned file nodes");
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
                file.setCreatedAt(trantor::Date::now());
                file.setUpdatedAt(trantor::Date::now());
                co_await file_mapper.insert(file);
            }

            co_await trash_mapper.deleteByPrimaryKey(trash_id);

            result.status = "success";
            result.folder_id = root_new_id;
            result.path = folder_path;

            Logger::Info() << "Folder tree restored successfully: trash_id=" << trash_id
                           << ", folder_id=" << root_new_id << ", folder_count="
                           << (snapshot->folders.size() + 1) << ", file_count=" << snapshot->files.size();

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to restore folder: trash_id=" << trash_item.id << " - "
                            << e.base().what();
            result.status = "failed";
            result.code = static_cast<uint16_t>(ErrorCode::InternalError);
            result.message = "Failed to restore folder";
        } catch (const std::exception& e) {
            Logger::Error() << "Failed to restore folder: trash_id=" << trash_item.id << " - "
                            << e.what();
            result.status = "failed";
            result.code = static_cast<uint16_t>(ErrorCode::InternalError);
            result.message = "Failed to restore folder";
        }
    }

    auto TrashService::DeleteFile(uint64_t trash_id, uint64_t user_id, BatchResultItem& result)
        -> drogon::Task<uint64_t> {

        try {
            CoroMapper<Trash> trash_mapper(m_db_client);
            auto trash_model = co_await trash_mapper.findOne(
                Criteria(Trash::Cols::_id, CompareOperator::EQ, trash_id)
            );

            TrashLifecycleRecord trash_item;
            trash_item.id = trash_model.getValueOfId();
            trash_item.user_id = trash_model.getValueOfUserId();
            trash_item.item_type = trash_model.getValueOfItemType();
            trash_item.item_id = trash_model.getValueOfItemId();
            trash_item.item_name = trash_model.getValueOfItemName();
            trash_item.item_size = trash_model.getValueOfItemSize();
            trash_item.original_folder_id = trash_model.getValueOfOriginalFolderId();
            trash_item.original_path = trash_model.getValueOfOriginalPath();
            trash_item.item_data =
                trash_model.getItemData() ? *trash_model.getItemData() : "";
            if (trash_model.getContentId()) {
                trash_item.content_id = *trash_model.getContentId();
            }

            co_return co_await DeleteFile(trash_item, user_id, result);
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to permanently delete file: trash_id=" << trash_id << " - "
                            << e.base().what();
            result.status = "failed";
            result.code = static_cast<uint16_t>(ErrorCode::InternalError);
            result.message = "Failed to permanently delete file";
            co_return 0;
        }
    }

    auto TrashService::DeleteFile(
        const TrashLifecycleRecord& trash_item,
        uint64_t user_id,
        BatchResultItem& result
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
            Logger::Warn() << "Cannot permanently delete trash file without valid content_id: trash_id="
                           << trash_item.id;
            result.status = "failed";
            result.code = static_cast<uint16_t>(content_id_result.error().code);
            result.message = content_id_result.error().message;
            result.field = "content_id";
            result.value = trash_item.content_id.has_value() ? std::to_string(trash_item.content_id.value()) : "NULL";
            co_return 0;
        }

        try {
            auto delete_result = co_await PermanentlyDeleteTrashItems({ trash_item }, true);
            auto blob_stats = co_await CleanupVerifiedZeroRefBlobs(
                delete_result.zero_ref_content_ids,
                "manual-file-delete"
            );
            (void)blob_stats;

            result.status = "success";
            result.freed_space = delete_result.freed_space;
            Logger::Info() << "File permanently deleted: trash_id=" << trash_item.id
                           << ", freed_space=" << delete_result.freed_space;
            co_return delete_result.freed_space;
        } catch (const std::exception& e) {
            Logger::Error() << "Failed to permanently delete file: trash_id=" << trash_item.id << " - "
                            << e.what();
            result.status = "failed";
            result.code = static_cast<uint16_t>(ErrorCode::InternalError);
            result.message = "Failed to permanently delete file";
            co_return 0;
        }
    }

    auto TrashService::DeleteFolder(uint64_t trash_id, uint64_t user_id, BatchResultItem& result)
        -> drogon::Task<uint64_t> {

        try {
            CoroMapper<Trash> trash_mapper(m_db_client);
            auto trash_model = co_await trash_mapper.findOne(
                Criteria(Trash::Cols::_id, CompareOperator::EQ, trash_id)
            );

            TrashLifecycleRecord trash_item;
            trash_item.id = trash_model.getValueOfId();
            trash_item.user_id = trash_model.getValueOfUserId();
            trash_item.item_type = trash_model.getValueOfItemType();
            trash_item.item_id = trash_model.getValueOfItemId();
            trash_item.item_name = trash_model.getValueOfItemName();
            trash_item.item_size = trash_model.getValueOfItemSize();
            trash_item.original_folder_id = trash_model.getValueOfOriginalFolderId();
            trash_item.original_path = trash_model.getValueOfOriginalPath();
            trash_item.item_data =
                trash_model.getItemData() ? *trash_model.getItemData() : "";
            if (trash_model.getContentId()) {
                trash_item.content_id = *trash_model.getContentId();
            }

            co_return co_await DeleteFolder(trash_item, user_id, result);
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to permanently delete folder: trash_id=" << trash_id << " - "
                            << e.base().what();
            result.status = "failed";
            result.code = static_cast<uint16_t>(ErrorCode::InternalError);
            result.message = "Failed to permanently delete folder";
            co_return 0;
        }
    }

    auto TrashService::DeleteFolder(
        const TrashLifecycleRecord& trash_item,
        uint64_t user_id,
        BatchResultItem& result
    ) -> drogon::Task<uint64_t> {

        if (trash_item.user_id != user_id || trash_item.item_type != "folder") {
            result.status = "failed";
            result.code = static_cast<uint16_t>(ErrorCode::ResourceNotFound);
            result.message = "Trash item not found";
            co_return 0;
        }

        try {
            auto delete_result = co_await PermanentlyDeleteTrashItems({ trash_item }, true);
            auto blob_stats = co_await CleanupVerifiedZeroRefBlobs(
                delete_result.zero_ref_content_ids,
                "manual-folder-delete"
            );
            (void)blob_stats;

            auto snapshot_content_ids = ExtractSnapshotContentIds(trash_item.item_data);
            result.status = "success";
            result.freed_space = delete_result.freed_space;

            Logger::Info() << "Folder permanently deleted: trash_id=" << trash_item.id
                           << ", freed_space=" << delete_result.freed_space
                           << ", snapshot_file_count=" << snapshot_content_ids.size();

            co_return delete_result.freed_space;
        } catch (const std::exception& e) {
            Logger::Error() << "Failed to permanently delete folder: trash_id=" << trash_item.id << " - "
                            << e.what();
            result.status = "failed";
            result.code = static_cast<uint16_t>(ErrorCode::InternalError);
            result.message = "Failed to permanently delete folder";
            co_return 0;
        }
    }

    auto TrashService::PermanentlyDeleteTrashItems(
        const std::vector<TrashLifecycleRecord>& trash_items,
        bool require_valid_file_content
    ) -> drogon::Task<PermanentDeleteResult> {
        PermanentDeleteResult result;
        if (trash_items.empty()) {
            co_return result;
        }

        std::shared_ptr<drogon::orm::Transaction> transaction;
        try {
            transaction = co_await m_db_client->newTransactionCoro();

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
                        Logger::Warn() << "Skip permanent delete for legacy trash file without valid content_id: trash_id="
                                       << item.id << ", user_id=" << item.user_id;
                        continue;
                    }

                    if (content_id_result->source ==
                        disk::services::trash_content_internal::ContentIdSource::ItemData) {
                        Logger::Debug() << "Resolved legacy trash content_id from item_data during permanent delete: trash_id="
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
                co_return result;
            }

            if (!content_ref_decrements.empty()) {
                disk::content::ContentService content_service(m_db_client);
                auto zero_ref_contents = co_await content_service.DecrementRefCounts(
                    transaction,
                    content_ref_decrements
                );
                result.zero_ref_content_ids.reserve(zero_ref_contents.size());
                for (const auto& content : zero_ref_contents) {
                    result.zero_ref_content_ids.push_back(content.id);
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
                    co_await quota_service.AdjustUsedStorage(transaction, user_id, delta);
                }
            }

            result.deleted_count = static_cast<int>(trash_ids.size());
            transaction.reset();
            co_return result;
        } catch (...) {
            if (transaction) {
                try {
                    transaction->rollback();
                } catch (const std::exception& rollback_error) {
                    Logger::Error() << "Trash permanent-delete rollback failed: "
                                    << rollback_error.what();
                }
            }
            throw;
        }
    }

    auto TrashService::CleanupVerifiedZeroRefBlobs(
        const std::vector<uint64_t>& content_ids,
        const std::string& log_context
    ) -> drogon::Task<BlobCleanupStats> {
        BlobCleanupStats stats;
        if (content_ids.empty()) {
            co_return stats;
        }

        std::vector<uint64_t> unique_content_ids = content_ids;
        std::sort(unique_content_ids.begin(), unique_content_ids.end());
        unique_content_ids.erase(
            std::unique(unique_content_ids.begin(), unique_content_ids.end()),
            unique_content_ids.end()
        );

        disk::content::ContentService content_service(m_db_client);
        auto verified_contents = co_await content_service.VerifyZeroRefContents(
            m_db_client,
            unique_content_ids
        );
        stats.verified_count = static_cast<int>(verified_contents.size());

        if (verified_contents.size() < unique_content_ids.size()) {
            Logger::Info() << "[" << log_context << "] blob safety check: candidates="
                           << unique_content_ids.size()
                           << " verified=" << verified_contents.size()
                           << " reclaimed_by_concurrent="
                           << (unique_content_ids.size() - verified_contents.size());
        }

        auto* blob_store = disk::storage::StorageMgr::GetBlobStore();
        if (blob_store == nullptr) {
            Logger::Warn() << "Storage manager is not initialized, skip " << log_context
                           << " blob cleanup: blob_count=" << verified_contents.size();
            co_return stats;
        }

        std::vector<std::filesystem::path> paths_to_delete;
        paths_to_delete.reserve(verified_contents.size());
        for (const auto& content : verified_contents) {
            paths_to_delete.emplace_back(content.storage_path);
        }

        auto delete_results = co_await ParallelDeletePaths(blob_store, paths_to_delete, MAX_PARALLEL_DELETE_PATHS);
        for (size_t i = 0; i < delete_results.size(); ++i) {
            if (!delete_results[i].has_value()) {
                Logger::Warn() << "Failed to cleanup " << log_context << " blob: storage_path="
                               << paths_to_delete[i] << ", error_code="
                               << static_cast<uint32_t>(delete_results[i].error().code)
                               << ", error_message=" << delete_results[i].error().message;
            } else {
                stats.deleted_count++;
                Logger::Info() << "Blob cleanup completed for " << log_context
                               << ": storage_path=" << paths_to_delete[i];
            }
        }

        co_return stats;
    }

    auto TrashService::GenerateUniqueFilename(
        uint64_t folder_id,
        const std::string& name,
        uint64_t user_id,
        bool is_file
    ) -> drogon::Task<std::string> {

        auto base_name = ExtractBaseName(name);
        auto extension = ExtractExtension(name);

        int counter = 1;
        std::string new_name;

        while (true) {
            if (is_file) {
                if (extension.empty()) {
                    new_name = base_name + " (" + std::to_string(counter) + ")";
                } else {
                    new_name = base_name + " (" + std::to_string(counter) + ")." + extension;
                }
            } else {
                new_name = name + " (" + std::to_string(counter) + ")";
            }

            bool exists = false;
            if (is_file) {
                exists = co_await IsFilenameExists(folder_id, new_name, user_id);
            } else {
                exists = co_await IsFolderNameExists(folder_id, new_name, user_id);
            }

            if (!exists) {
                co_return new_name;
            }

            counter++;

            if (counter > 1000) {
                Logger::Warn() << "Unable to generate unique filename, max attempts reached: " << name;
                co_return new_name;
            }
        }
    }

    auto TrashService::IsFilenameExists(
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

    auto TrashService::IsFolderNameExists(
        uint64_t folder_id,
        const std::string& foldername,
        uint64_t user_id
    ) const -> drogon::Task<bool> {

        try {
            CoroMapper<Folders> mapper(m_db_client);
            auto count = co_await mapper.count(
                Criteria(Folders::Cols::_user_id, CompareOperator::EQ, user_id) &&
                Criteria(Folders::Cols::_parent_id, CompareOperator::EQ, folder_id) &&
                Criteria(Folders::Cols::_name, CompareOperator::EQ, foldername)
            );

            co_return count > 0;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to check folder name: " << e.base().what();
            co_return false;
        }
    }

    auto TrashService::IsFolderExists(uint64_t folder_id, uint64_t user_id) const
        -> drogon::Task<bool> {
        if (folder_id == 0) {
            co_return true;
        }

        try {
            CoroMapper<Folders> mapper(m_db_client);
            auto folder = co_await mapper.findOne(
                Criteria(Folders::Cols::_id, CompareOperator::EQ, folder_id)
            );

            co_return folder.getValueOfUserId() == user_id;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Debug() << "Folder not found: folder_id=" << folder_id;
            co_return false;
        }
    }

    auto TrashService::UpdateStorageUsed(uint64_t user_id, int64_t delta) -> drogon::Task<void> {
        disk::quota::QuotaService quota_service(m_db_client);
        co_await quota_service.AdjustUsedStorage(m_db_client, user_id, delta);
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
