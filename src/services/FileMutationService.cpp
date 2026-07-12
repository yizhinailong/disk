/**
 * @file FileMutationService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件变更服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "FileMutationService.hpp"

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <unordered_set>

#include <json/writer.h>

#include "FileServiceUtils.hpp"
#include "TransactionRunner.hpp"
#include "TrashService.hpp"
#include "models/FileContents.hpp"
#include "models/Files.hpp"
#include "models/Folders.hpp"
#include "services/ContentService.hpp"
#include "services/QuotaService.hpp"
#include "utils/BatchUtils.hpp"
#include "utils/RedisKeyPrefix.hpp"

namespace disk::file {

    using disk::utils::BatchUtils;
    using disk::utils::DEFAULT_BATCH_CHUNK_SIZE;
    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::FileContents;
    using drogon_model::disk::Files;
    using drogon_model::disk::Folders;

    /// ==================== 构造函数 ====================

    FileMutationService::FileMutationService(drogon::orm::DbClientPtr db_client, storage::IFileStorage* storage)
        : m_db_client(std::move(db_client)),
          m_file_repository(m_db_client),
          m_folder_repository(m_db_client),
          m_storage(storage) {
        Logger::Debug() << "FileMutationService initialization completed";
    }

    /// ==================== Rename ====================

    auto FileMutationService::Rename(uint64_t file_id, std::string new_name, uint64_t user_id)
        -> drogon::Task<Result<RenameResponse>> {

        Logger::Debug() << "Starting rename file: file_id=" << file_id << ", new_name=\"" << new_name
                        << "\""
                        << ", user_id=" << user_id;

        try {
            auto file = co_await m_file_repository.FindOwnedFile(m_db_client, file_id, user_id);
            if (!file) {
                co_return std::unexpected(ErrorInfo(ErrorCode::FileNotFound));
            }

            auto folder_id = file->getValueOfFolderId();
            if (file->getValueOfName() != new_name && co_await IsFilenameExists(folder_id, new_name, user_id)) {
                Logger::Warn() << "Target folder already has file with same name: " << new_name;
                co_return std::unexpected(ErrorInfo(ErrorCode::FileAlreadyExists));
            }

            auto folder_location_result = co_await m_folder_repository.ResolveOwnedFolderLocation(m_db_client, folder_id, user_id);
            if (!folder_location_result) {
                co_return std::unexpected(folder_location_result.error());
            }

            auto updated_at = trantor::Date::now();
            auto new_path = utils::BuildFilePath(folder_location_result->path, new_name);
            auto updated = co_await m_file_repository.RenameOwnedFile(
                m_db_client,
                file_id,
                user_id,
                new_name,
                ExtractExtension(new_name),
                new_path,
                updated_at
            );
            if (!updated) {
                co_return std::unexpected(ErrorInfo(ErrorCode::FileNotFound));
            }

            Logger::Info() << "File rename successful: file_id=" << file_id << ", new_name=\"" << new_name
                           << "\"";

            RenameResponse response;
            response.id = file->getValueOfId();
            response.name = new_name;
            response.updated_at = updated_at.toDbStringLocal();

            co_await InvalidateFileListCache(user_id, { folder_id });
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "File not found or no permission: file_id=" << file_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::FileNotFound));
        }
    }

    /// ==================== Move ====================

    auto FileMutationService::Move(MoveRequest request, uint64_t user_id)
        -> drogon::Task<Result<MoveResponse>> {

        Logger::Debug() << "Starting move drive items: file_ids.size()=" << request.file_ids.size()
                        << ", folder_ids.size()=" << request.folder_ids.size()
                        << ", target_folder_id=" << request.target_folder_id << ", user_id=" << user_id;

        auto target_location_result =
            co_await m_folder_repository.ResolveOwnedFolderLocation(m_db_client, request.target_folder_id, user_id);
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

        TransactionRunner transaction_runner(
            m_db_client,
            ErrorInfo(ErrorCode::InternalError, "Failed to move items")
        );
        auto transaction_result = co_await transaction_runner.Run(
            [&](const drogon::orm::DbClientPtr& transaction) -> drogon::Task<Result<void>> {
                if (!file_ids.empty()) {
                    auto chunks = BatchUtils::Chunk(file_ids, DEFAULT_BATCH_CHUNK_SIZE);
                    for (const auto& chunk : chunks) {
                        if (chunk.empty()) {
                            continue;
                        }

                        auto fetched_files = co_await m_file_repository.FetchOwnedFilesByIds(
                            transaction,
                            chunk,
                            user_id
                        );

                        std::unordered_map<uint64_t, std::pair<uint64_t, std::string>> files;
                        files.reserve(fetched_files.size());
                        std::vector<std::string> candidate_names;
                        candidate_names.reserve(fetched_files.size());
                        for (const auto& file : fetched_files) {
                            auto id = file.getValueOfId();
                            auto folder_id = file.getValueOfFolderId();
                            auto name = file.getValueOfName();
                            files[id] = { folder_id, name };
                            if (folder_id != request.target_folder_id) {
                                candidate_names.push_back(name);
                            }
                        }

                        auto occupied_names = co_await utils::QueryOccupiedNames(
                            transaction,
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
                            auto updated = co_await m_file_repository.UpdateFileLocation(
                                transaction,
                                file_id,
                                user_id,
                                request.target_folder_id,
                                utils::BuildFilePath(target_location.path, name),
                                updated_at
                            );
                            if (!updated) {
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
                            co_await m_folder_repository.ApplyItemCountDelta(
                                transaction,
                                folder_id,
                                user_id,
                                delta,
                                trantor::Date::now()
                            );
                        }
                    }
                }

                std::unordered_map<uint64_t, utils::FolderDeletePlan> folder_plans =
                    co_await m_folder_repository.FetchBatchFolderDeletePlans(transaction, folder_ids, user_id);

                auto top_level_folder_ids = utils::FilterCoveredFolderIds(folder_ids, folder_plans);
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

                auto occupied_folder_names = co_await utils::QueryOccupiedFolderNames(
                    transaction,
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
                        co_return std::unexpected(ErrorInfo(
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
                    auto new_prefix = utils::BuildFolderPath(target_location.path, folder_name);
                    auto depth_delta = static_cast<int>(target_location.depth) + 1 -
                                       static_cast<int>(root.getValueOfDepth());

                    for (const auto& folder : plan.folders) {
                        auto old_path = folder.getValueOfPath();
                        auto new_path = new_prefix + old_path.substr(old_prefix.size());
                        if (folder.getValueOfId() == root.getValueOfId()) {
                            co_await m_folder_repository.UpdateFolderLocationForMove(
                                transaction,
                                folder.getValueOfId(),
                                user_id,
                                request.target_folder_id,
                                new_path,
                                depth_delta,
                                trantor::Date::now()
                            );
                        } else {
                            co_await m_folder_repository.UpdateFolderPathForMove(
                                transaction,
                                folder.getValueOfId(),
                                user_id,
                                new_path,
                                depth_delta,
                                trantor::Date::now()
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
                        co_await m_file_repository.UpdateFilePath(
                            transaction,
                            file.getValueOfId(),
                            user_id,
                            utils::BuildFilePath(path_it->second, file.getValueOfName()),
                            trantor::Date::now()
                        );
                    }

                    if (old_parent_id > 0) {
                        co_await m_folder_repository.ApplyItemCountDelta(
                            transaction,
                            old_parent_id,
                            user_id,
                            -1,
                            trantor::Date::now()
                        );
                    }
                    if (request.target_folder_id > 0) {
                        co_await m_folder_repository.ApplyItemCountDelta(
                            transaction,
                            request.target_folder_id,
                            user_id,
                            1,
                            trantor::Date::now()
                        );
                    }

                    ++moved_folder_count;
                }

                co_return {};
            }
        );
        if (!transaction_result) {
            co_return std::unexpected(transaction_result.error());
        }

        Logger::Info() << "Move completed: moved_file_count=" << moved_file_count
                       << ", moved_folder_count=" << moved_folder_count;

        /// Preserve existing target-folder-only file list cache invalidation after successful move.
        co_await InvalidateFileListCache(user_id, { request.target_folder_id });

        MoveResponse response;
        response.moved_file_count = moved_file_count;
        response.moved_folder_count = moved_folder_count;
        response.moved_count = moved_file_count + moved_folder_count;
        co_return response;
    }

    /// ==================== Copy ====================

    auto FileMutationService::Copy(CopyRequest request, uint64_t user_id)
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

        auto target_location_result = co_await utils::ResolveFolderLocation(
            m_db_client,
            request.target_folder_id,
            user_id
        );
        if (!target_location_result) {
            Logger::Warn() << "Target folder not found or no permission: folder_id="
                           << request.target_folder_id;
            co_return std::unexpected(target_location_result.error());
        }

        std::unordered_map<uint64_t, utils::FolderDeletePlan> folder_plans =
            co_await utils::FetchBatchFolderDeletePlans(m_db_client, requested_folder_ids, user_id);

        auto top_level_folder_ids = utils::FilterCoveredFolderIds(requested_folder_ids, folder_plans);
        auto covered_file_ids = utils::CollectCoveredFileIds(top_level_folder_ids, folder_plans);

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
                    "SELECT id, user_id, folder_id, content_id, name, extension, size, mime_type, path, " "is_favorite, download_count, last_accessed_at, created_at, updated_at " "FROM files WHERE id IN (" + BatchUtils::BuildSafeNumericInClause(chunk) + ") AND user_id = $1",
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
            disk::quota::QuotaService quota_service(m_db_client);
            auto quota_result = co_await quota_service.ConsumeUsedStorage(
                m_db_client,
                user_id,
                total_copy_size
            );
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
        disk::content::ContentService content_service(m_db_client);

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
                occupied_names = co_await utils::QueryOccupiedNames(
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
                    existing_content_ids = co_await content_service.FindExistingIds(m_db_client, content_ids);
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

                auto incremented_ids = co_await content_service.IncrementRefCounts(
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
            occupied_root_folder_names = co_await utils::QueryOccupiedFolderNames(
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
                    existing_content_ids = co_await content_service.FindExistingIds(m_db_client, content_ids);
                } catch (const drogon::orm::DrogonDbException& e) {
                    Logger::Warn() << "Folder copy content query failed, skipping folder_id=" << folder_id
                                   << ": " << e.base().what();
                    continue;
                }
            }

            std::shared_ptr<drogon::orm::Transaction> txn;
            try {
                txn = co_await m_db_client->newTransactionCoro();

                auto incremented_ids = co_await content_service.IncrementRefCounts(
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

                auto root_path = utils::BuildFolderPath(target_location_result->path, root_name);
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
                folder_mappings.push_back({ .old_id = plan.root.getValueOfId(), .new_id = inserted_root.getValueOfId() });
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

                    auto folder_path = utils::BuildFolderPath(parent_path_it->second, folder.getValueOfName());
                    auto depth_delta = folder.getValueOfDepth() > plan.root.getValueOfDepth() ? folder.getValueOfDepth() - plan.root.getValueOfDepth() : 1;

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
                    folder_mappings.push_back({ .old_id = folder.getValueOfId(), .new_id = inserted_folder.getValueOfId() });
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
                    copied_file.setPath(utils::BuildFilePath(path_it->second, file.getValueOfName()));
                    copied_file.setIsFavorite(0);
                    copied_file.setDownloadCount(0);
                    copied_file.setCreatedAt(trantor::Date::now());
                    copied_file.setUpdatedAt(trantor::Date::now());

                    auto inserted_file = co_await file_mapper.insert(copied_file);
                    file_mappings.push_back({ .old_id = file.getValueOfId(), .new_id = inserted_file.getValueOfId() });
                    ++file_count;
                    copied_size += file.getValueOfSize();
                }

                if (request.target_folder_id > 0) {
                    co_await txn->execSqlCoro(
                        "UPDATE folders SET item_count = item_count + 1, updated_at = $1 " "WHERE id = $2 AND user_id = $3",
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

        co_await InvalidateFileListCache(user_id, { request.target_folder_id });

        co_return response;
    }

    /// ==================== Delete ====================

    auto FileMutationService::Delete(DeleteRequest request, uint64_t user_id)
        -> drogon::Task<Result<DeleteResponse>> {

        Logger::Debug() << "Starting delete items: file_ids.size()=" << request.file_ids.size()
                        << ", folder_ids.size()=" << request.folder_ids.size() << ", user_id=" << user_id;

        auto delete_start = std::chrono::steady_clock::now();

        disk::trash::TrashService trash_service(m_db_client);
        disk::trash::MoveToTrashRequest move_request{
            .file_ids = std::move(request.file_ids),
            .folder_ids = std::move(request.folder_ids),
        };

        auto move_result = co_await trash_service.MoveToTrash(std::move(move_request), user_id);
        if (!move_result) {
            co_return std::unexpected(move_result.error());
        }

        auto delete_elapsed = std::chrono::steady_clock::now() - delete_start;
        Logger::Info() << "FileMutationService::Delete completed: deleted_count=" << move_result->deleted_count
                       << ", deleted_file_count=" << move_result->deleted_file_count
                       << ", deleted_folder_count=" << move_result->deleted_folder_count
                       << ", removed_file_rows=" << move_result->removed_file_ids.size()
                       << ", removed_folder_rows=" << move_result->removed_folder_ids.size()
                       << ", elapsed_ms="
                       << std::chrono::duration_cast<std::chrono::milliseconds>(delete_elapsed).count();

        DeleteResponse response;
        response.deleted_count = move_result->deleted_count;
        response.deleted_file_count = move_result->deleted_file_count;
        response.deleted_folder_count = move_result->deleted_folder_count;

        co_return response;
    }

    /// ==================== 私有辅助方法 ====================

    auto FileMutationService::CheckStorageQuota(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        uint64_t file_size
    ) const -> drogon::Task<Result<void>> {
        disk::quota::QuotaService quota_service(m_db_client);
        co_return co_await quota_service.ConsumeUsedStorage(client, user_id, file_size);
    }

    auto FileMutationService::UpdateStorageUsed(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        int64_t delta
    ) -> drogon::Task<void> {

        disk::quota::QuotaService quota_service(m_db_client);
        co_await quota_service.AdjustUsedStorage(client, user_id, delta);
    }

    auto FileMutationService::InsertCopiedFiles(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        uint64_t target_folder_id,
        const std::vector<std::pair<uint64_t, const drogon_model::disk::Files*>>& valid_items
    ) -> drogon::Task<std::vector<std::pair<uint64_t, uint64_t>>> {

        std::vector<std::pair<uint64_t, uint64_t>> id_mappings;

        if (valid_items.empty()) {
            co_return id_mappings;
        }

        auto target_location_result = co_await utils::ResolveFolderLocation(client, target_folder_id, user_id);
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
            auto result = co_await utils::ExecSqlWithBindings(
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
                               << utils::BuildFilePath(target_location_result->path, file.getValueOfName())
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

    auto FileMutationService::ExtractExtension(const std::string& filename) -> std::string {
        auto pos = filename.rfind('.');
        if (pos == std::string::npos || pos == filename.length() - 1) {
            return "";
        }
        return filename.substr(pos + 1);
    }

    auto FileMutationService::IsFilenameExists(
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

    auto FileMutationService::InvalidateFileListCache(uint64_t user_id, const std::vector<uint64_t>& folder_ids)
        -> drogon::Task<void> {
        for (const auto folder_id : folder_ids) {
            const auto prefix = disk::redis::RedisKeyPrefix::BuildFileListCachePrefix(user_id, folder_id);
            auto delete_result = co_await m_redis_service->DeleteByPrefix(prefix);
            if (!delete_result) {
                Logger::Warn() << "Failed to invalidate file list cache by prefix: " << prefix;
            }
        }
    }

} // namespace disk::file
