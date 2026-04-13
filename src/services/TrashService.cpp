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
#include <optional>
#include <unordered_map>

#include <drogon/orm/CoroMapper.h>
#include <drogon/orm/Criteria.h>

#include "models/FileContents.hpp"
#include "models/Files.hpp"
#include "models/Folders.hpp"
#include "models/Trash.hpp"
#include "models/Users.hpp"
#include "services/TrashContentIdResolver.hpp"
#include "storage/StorageMgr.hpp"
#include "utils/BatchUtils.hpp"

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

    TrashService::TrashService(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
        LOG_DEBUG << "TrashService initialization completed";
    }

    // ==================== 公共方法实现 ====================

    auto TrashService::List(uint64_t user_id, int page, int page_size)
        -> drogon::Task<Result<std::vector<TrashItemResponse>>> {

        LOG_INFO << "Fetching trash list: user_id=" << user_id << ", page=" << page
                 << ", page_size=" << page_size;

        try {
            auto offset = (page - 1) * page_size;

            auto result = co_await m_db_client->execSqlCoro(
                "SELECT id, user_id, item_type, item_id, item_name, item_size, " "original_" "folde" "r_" "id," " " "original_path, " "item_data, " "deleted_at, " "expires_at " "FRO" "M " "tra" "sh " "WHERE user_id = ? " "ORDER BY deleted_at DESC " "LIMIT ? OFFSET ?",
                user_id,
                page_size,
                offset
            );

            std::vector<TrashItemResponse> responses;
            responses.reserve(result.size());

            for (size_t i = 0; i < result.size(); ++i) {
                const auto& row = result[i];
                TrashItemResponse response;
                response.id = row["id"].as<uint64_t>();
                response.type = row["item_type"].as<std::string>();
                response.original_id = row["item_id"].as<uint64_t>();
                response.name = row["item_name"].as<std::string>();
                response.size = row["item_size"].as<uint64_t>();
                response.original_path = row["original_path"].as<std::string>();
                response.deleted_at = row["deleted_at"].as<std::string>();
                response.expires_at = row["expires_at"].as<std::string>();
                responses.push_back(response);
            }

            LOG_DEBUG << "Found " << responses.size() << " trash items";
            co_return responses;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Database error fetching trash list: user_id=" << user_id << " - "
                      << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to fetch trash list, please try again later"
            ));
        } catch (const std::exception& e) {
            LOG_ERROR << "Unknown error fetching trash list: user_id=" << user_id << " - "
                      << e.what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to fetch trash list, please try again later"
            ));
        }
    }

    auto TrashService::Count(uint64_t user_id) -> drogon::Task<Result<int>> {
        LOG_DEBUG << "Counting trash items: user_id=" << user_id;

        try {
            CoroMapper<Trash> mapper(m_db_client);
            auto count = co_await mapper.count(
                Criteria(Trash::Cols::_user_id, CompareOperator::EQ, user_id)
            );
            co_return static_cast<int>(count);

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to count trash items: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to count trash items")
            );
        }
    }

    auto TrashService::Restore(uint64_t user_id, const std::vector<uint64_t>& trash_ids)
        -> drogon::Task<Result<BatchRestoreResponse>> {

        LOG_INFO << "Batch restoring trash items: user_id=" << user_id
                 << ", count=" << trash_ids.size();

        BatchRestoreResponse response;
        response.summary.total = static_cast<int>(trash_ids.size());
        response.results.reserve(trash_ids.size());

        std::unordered_map<uint64_t, PrefetchedTrashItem> trash_items_by_id;
        trash_items_by_id.reserve(trash_ids.size());

        try {
            auto chunks = BatchUtils::Chunk(trash_ids, DEFAULT_BATCH_CHUNK_SIZE);
            for (const auto& chunk : chunks) {
                if (chunk.empty()) {
                    continue;
                }

                auto rows = co_await m_db_client->execSqlCoro(
                    "SELECT id, user_id, item_type, item_id, item_name, item_size, " "original_folder_id, original_path, item_data, content_id " "FROM trash WHERE id IN (" + BatchUtils::BuildSafeNumericInClause(chunk) + ")"
                );

                for (const auto& row : rows) {
                    PrefetchedTrashItem item;
                    item.id = row["id"].as<uint64_t>();
                    item.user_id = row["user_id"].as<uint64_t>();
                    item.item_type = row["item_type"].as<std::string>();
                    item.item_id = row["item_id"].as<uint64_t>();
                    item.item_name = row["item_name"].as<std::string>();
                    item.item_size = row["item_size"].as<uint64_t>();
                    item.original_folder_id = row["original_folder_id"].as<uint64_t>();
                    item.original_path = row["original_path"].as<std::string>();
                    item.item_data = row["item_data"].as<std::string>();
                    if (!row["content_id"].isNull()) {
                        item.content_id = row["content_id"].as<uint64_t>();
                    }

                    trash_items_by_id[item.id] = std::move(item);
                }
            }
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to batch fetch trash items for restore: user_id=" << user_id
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

        LOG_INFO << "Batch restore completed: total=" << response.summary.total
                 << ", success=" << response.summary.success_count
                 << ", failure=" << response.summary.failure_count;

        co_return response;
    }

    auto TrashService::Delete(uint64_t user_id, const std::vector<uint64_t>& trash_ids)
        -> drogon::Task<Result<BatchDeleteResponse>> {

        LOG_INFO << "Batch permanently deleting trash items: user_id=" << user_id
                 << ", count=" << trash_ids.size();

        BatchDeleteResponse response;
        response.summary.total = static_cast<int>(trash_ids.size());
        response.results.reserve(trash_ids.size());

        uint64_t total_freed_space = 0;

        std::unordered_map<uint64_t, PrefetchedTrashItem> trash_items_by_id;
        trash_items_by_id.reserve(trash_ids.size());

        try {
            auto chunks = BatchUtils::Chunk(trash_ids, DEFAULT_BATCH_CHUNK_SIZE);
            for (const auto& chunk : chunks) {
                if (chunk.empty()) {
                    continue;
                }

                auto rows = co_await m_db_client->execSqlCoro(
                    "SELECT id, user_id, item_type, item_id, item_name, item_size, " "original_folder_id, original_path, item_data, content_id " "FROM trash WHERE id IN (" + BatchUtils::BuildSafeNumericInClause(chunk) + ")"
                );

                for (const auto& row : rows) {
                    PrefetchedTrashItem item;
                    item.id = row["id"].as<uint64_t>();
                    item.user_id = row["user_id"].as<uint64_t>();
                    item.item_type = row["item_type"].as<std::string>();
                    item.item_id = row["item_id"].as<uint64_t>();
                    item.item_name = row["item_name"].as<std::string>();
                    item.item_size = row["item_size"].as<uint64_t>();
                    item.original_folder_id = row["original_folder_id"].as<uint64_t>();
                    item.original_path = row["original_path"].as<std::string>();
                    item.item_data = row["item_data"].as<std::string>();
                    if (!row["content_id"].isNull()) {
                        item.content_id = row["content_id"].as<uint64_t>();
                    }

                    trash_items_by_id[item.id] = std::move(item);
                }
            }
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to batch fetch trash items for delete: user_id=" << user_id
                      << " - " << e.base().what();
            co_return std::unexpected(ErrorInfo(
                ErrorCode::InternalError,
                "Failed to delete trash items, please try again later"
            ));
        }

        for (auto trash_id : trash_ids) {
            BatchResultItem result;
            result.trash_id = trash_id;
            uint64_t freed_space = 0;

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
                freed_space = co_await DeleteFile(trash_item, user_id, result);
            } else if (trash_item.item_type == "folder") {
                freed_space = co_await DeleteFolder(trash_item, user_id, result);
            } else {
                result.status = "failed";
                result.code = static_cast<uint16_t>(ErrorCode::ValidationFailed);
                result.message = "Unknown item type";
                response.summary.failure_count++;
            }

            if (result.status == "success") {
                response.summary.success_count++;
                total_freed_space += freed_space;
            }
            response.results.push_back(result);
        }

        if (total_freed_space > 0) {
            co_await UpdateStorageUsed(user_id, -static_cast<int64_t>(total_freed_space));
            LOG_DEBUG << "Storage space freed: user_id=" << user_id
                      << ", freed=" << total_freed_space;
        }

        LOG_INFO << "Batch delete completed: total=" << response.summary.total
                 << ", success=" << response.summary.success_count
                 << ", failure=" << response.summary.failure_count
                 << ", freed_space=" << total_freed_space;

        co_return response;
    }

    auto TrashService::DeleteAll(uint64_t user_id) -> drogon::Task<Result<DeleteAllResponse>> {
        LOG_INFO << "Emptying trash: user_id=" << user_id;

        DeleteAllResponse response;
        response.deleted_count = 0;
        response.freed_space = 0;

        try {
            auto trash_rows = co_await m_db_client->execSqlCoro(
                "SELECT id, user_id, item_type, item_id, item_name, item_size, " "original_folder_id, original_path, item_data, content_id " "FROM trash WHERE user_id = ?",
                user_id
            );

            std::vector<PrefetchedTrashItem> trash_items;
            trash_items.reserve(trash_rows.size());
            for (const auto& row : trash_rows) {
                PrefetchedTrashItem item;
                item.id = row["id"].as<uint64_t>();
                item.user_id = row["user_id"].as<uint64_t>();
                item.item_type = row["item_type"].as<std::string>();
                item.item_id = row["item_id"].as<uint64_t>();
                item.item_name = row["item_name"].as<std::string>();
                item.item_size = row["item_size"].as<uint64_t>();
                item.original_folder_id = row["original_folder_id"].as<uint64_t>();
                item.original_path = row["original_path"].as<std::string>();
                item.item_data = row["item_data"].as<std::string>();
                if (!row["content_id"].isNull()) {
                    item.content_id = row["content_id"].as<uint64_t>();
                }
                trash_items.push_back(std::move(item));
            }

            auto chunks = BatchUtils::Chunk(trash_items, DEFAULT_BATCH_CHUNK_SIZE);
            for (const auto& chunk : chunks) {
                if (chunk.empty()) {
                    continue;
                }

                std::shared_ptr<drogon::orm::Transaction> transaction;
                try {
                    transaction = co_await m_db_client->newTransactionCoro();

                    uint64_t chunk_freed_space = 0;
                    int chunk_deleted_count = 0;

                    std::vector<uint64_t> chunk_trash_ids;
                    chunk_trash_ids.reserve(chunk.size());
                    std::vector<uint64_t> content_ids;

                    for (const auto& item : chunk) {
                        if (item.item_type != "file" && item.item_type != "folder") {
                            throw std::runtime_error("Unknown item type in trash chunk");
                        }

                        if (item.item_type == "file") {
                            auto content_id_result =
                                disk::services::trash_content_internal::ResolveRequiredContentId(
                                    item.content_id,
                                    item.item_data
                                );
                            if (!content_id_result.has_value()) {
                                LOG_WARN << "Skip DeleteAll for legacy trash file without valid content_id: trash_id="
                                         << item.id << ", user_id=" << user_id;
                                continue;
                            }

                            if (content_id_result->source ==
                                disk::services::trash_content_internal::ContentIdSource::ItemData) {
                                LOG_DEBUG << "Resolved legacy trash content_id from item_data during DeleteAll: trash_id="
                                          << item.id << ", content_id=" << content_id_result->value;
                            }

                            content_ids.push_back(content_id_result->value);
                        }

                        chunk_trash_ids.push_back(item.id);
                        chunk_freed_space += item.item_size;
                        chunk_deleted_count++;
                    }

                    if (chunk_trash_ids.empty()) {
                        continue;
                    }

                    std::vector<std::string> zero_ref_paths;
                    if (!content_ids.empty()) {
                        std::sort(content_ids.begin(), content_ids.end());
                        content_ids.erase(std::unique(content_ids.begin(), content_ids.end()), content_ids.end());

                        auto content_in_clause = BatchUtils::BuildSafeNumericInClause(content_ids);
                        co_await transaction->execSqlCoro(
                            "UPDATE file_contents " "SET ref_count = GREATEST(ref_count - 1, 0) " "WHERE id IN (" + content_in_clause + ")"
                        );

                        auto content_rows = co_await transaction->execSqlCoro(
                            "SELECT id, ref_count, storage_path " "FROM file_contents WHERE id IN (" + content_in_clause + ")"
                        );

                        for (const auto& row : content_rows) {
                            if (row["ref_count"].as<uint32_t>() == 0) {
                                zero_ref_paths.push_back(row["storage_path"].as<std::string>());
                            }
                        }
                    }

                    auto delete_result = co_await transaction->execSqlCoro(
                        "DELETE FROM trash WHERE id IN (" + BatchUtils::BuildSafeNumericInClause(chunk_trash_ids) + ")"
                    );

                    if (delete_result.affectedRows() != chunk_trash_ids.size()) {
                        throw std::runtime_error("Chunk delete affected rows mismatch");
                    }

                    response.deleted_count += chunk_deleted_count;
                    response.freed_space += chunk_freed_space;

                    if (!zero_ref_paths.empty()) {
                        auto* storage = disk::storage::StorageMgr::GetStorage();
                        if (storage == nullptr) {
                            LOG_WARN << "Storage manager is not initialized, skip blob cleanup for chunk: user_id="
                                     << user_id << ", blob_count=" << zero_ref_paths.size();
                        } else {
                            for (const auto& path : zero_ref_paths) {
                                auto delete_blob_result = co_await storage->DeletePath(path);
                                if (!delete_blob_result.has_value()) {
                                    LOG_WARN << "Failed to cleanup blob after DeleteAll chunk: storage_path="
                                             << path << ", error_code="
                                             << static_cast<uint32_t>(delete_blob_result.error().code)
                                             << ", error_message=" << delete_blob_result.error().message;
                                } else {
                                    LOG_INFO << "Blob cleanup completed after DeleteAll chunk: storage_path="
                                             << path;
                                }
                            }
                        }
                    }

                } catch (const std::exception& e) {
                    if (transaction) {
                        try {
                            transaction->rollback();
                        } catch (const std::exception& rollback_e) {
                            LOG_ERROR << "Chunk rollback failed when emptying trash: user_id="
                                      << user_id << " - " << rollback_e.what();
                        }
                    }

                    LOG_ERROR << "Failed to process DeleteAll chunk atomically: user_id="
                              << user_id << " - " << e.what();
                    continue;
                }
            }

            if (response.freed_space > 0) {
                co_await UpdateStorageUsed(user_id, -static_cast<int64_t>(response.freed_space));
            }

            LOG_INFO << "Trash emptied: user_id=" << user_id
                     << ", deleted=" << response.deleted_count
                     << ", freed_space=" << response.freed_space;

            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Database error emptying trash: user_id=" << user_id << " - "
                      << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to empty trash, please try again later")
            );
        } catch (const std::exception& e) {
            LOG_ERROR << "Unknown error emptying trash: user_id=" << user_id << " - " << e.what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to empty trash, please try again later")
            );
        }
    }

    // ==================== 私有方法实现 ====================

    auto TrashService::RestoreFile(uint64_t trash_id, uint64_t user_id, BatchResultItem& result)
        -> drogon::Task<void> {

        try {
            CoroMapper<Trash> trash_mapper(m_db_client);
            auto trash_model = co_await trash_mapper.findOne(
                Criteria(Trash::Cols::_id, CompareOperator::EQ, trash_id)
            );

            PrefetchedTrashItem trash_item;
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
            LOG_ERROR << "Failed to restore file: trash_id=" << trash_id << " - "
                      << e.base().what();
            result.status = "failed";
            result.code = static_cast<uint16_t>(ErrorCode::InternalError);
            result.message = "Failed to restore file";
        }
    }

    auto TrashService::RestoreFile(
        const PrefetchedTrashItem& trash_item,
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
                LOG_DEBUG << "Original folder not found, restoring to root: original_folder_id="
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
                    LOG_WARN << "Failed to get parent folder path, using root: folder_id="
                             << target_folder_id;
                    target_folder_id = 0;
                    parent_path = "/";
                }
            }

            auto final_name = item_name;
            if (co_await IsFilenameExists(target_folder_id, item_name, user_id)) {
                final_name =
                    co_await GenerateUniqueFilename(target_folder_id, item_name, user_id, true);
                LOG_DEBUG << "Filename conflict, auto-renamed: " << item_name << " -> "
                          << final_name;
            }

            auto content_id_result =
                disk::services::trash_content_internal::ResolveRequiredContentId(
                    trash_item.content_id,
                    trash_item.item_data
                );
            if (!content_id_result.has_value()) {
                LOG_WARN << "Cannot restore trash file without valid content_id: trash_id=" << trash_id;
                result.status = "failed";
                result.code = static_cast<uint16_t>(content_id_result.error().code);
                result.message = content_id_result.error().message;
                result.field = "content_id";
                result.value = trash_item.content_id.has_value() ? std::to_string(trash_item.content_id.value()) : "NULL";
                co_return;
            }

            if (content_id_result->source ==
                disk::services::trash_content_internal::ContentIdSource::ItemData) {
                LOG_DEBUG << "Resolved legacy trash content_id from item_data during restore: trash_id="
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

            LOG_INFO << "File restored successfully: trash_id=" << trash_id
                     << ", file_id=" << inserted_file.getValueOfId() << ", name=" << final_name
                     << ", path=" << file_path;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to restore file: trash_id=" << trash_item.id << " - "
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

            PrefetchedTrashItem trash_item;
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
            LOG_ERROR << "Failed to restore folder: trash_id=" << trash_id << " - "
                      << e.base().what();
            result.status = "failed";
            result.code = static_cast<uint16_t>(ErrorCode::InternalError);
            result.message = "Failed to restore folder";
        }
    }

    auto TrashService::RestoreFolder(
        const PrefetchedTrashItem& trash_item,
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
                    LOG_WARN << "Failed to get parent folder info, using root: folder_id="
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
                LOG_DEBUG << "Folder name conflict, auto-renamed: " << item_name << " -> "
                          << final_name;
            }

            std::string folder_path = parent_path + final_name + "/";
            uint32_t folder_depth = parent_depth + 1;

            Folders folder;
            folder.setUserId(user_id);
            folder.setParentId(target_parent_id);
            folder.setName(final_name);
            folder.setPath(folder_path);
            folder.setDepth(folder_depth);
            folder.setItemCount(0);
            folder.setCreatedAt(trantor::Date::now());
            folder.setUpdatedAt(trantor::Date::now());

            CoroMapper<Folders> folder_mapper(m_db_client);
            auto inserted_folder = co_await folder_mapper.insert(folder);

            CoroMapper<Trash> trash_mapper(m_db_client);
            co_await trash_mapper.deleteByPrimaryKey(trash_id);

            result.status = "success";
            result.folder_id = inserted_folder.getValueOfId();
            result.path = folder_path;

            LOG_INFO << "Folder restored successfully: trash_id=" << trash_id
                     << ", folder_id=" << inserted_folder.getValueOfId() << ", name=" << final_name
                     << ", path=" << folder_path << ", depth=" << folder_depth;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to restore folder: trash_id=" << trash_item.id << " - "
                      << e.base().what();
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

            PrefetchedTrashItem trash_item;
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
            LOG_ERROR << "Failed to permanently delete file: trash_id=" << trash_id << " - "
                      << e.base().what();
            result.status = "failed";
            result.code = static_cast<uint16_t>(ErrorCode::InternalError);
            result.message = "Failed to permanently delete file";
            co_return 0;
        }
    }

    auto TrashService::DeleteFile(
        const PrefetchedTrashItem& trash_item,
        uint64_t user_id,
        BatchResultItem& result
    ) -> drogon::Task<uint64_t> {

        try {
            auto trash_id = trash_item.id;
            auto item_size = trash_item.item_size;

            auto content_id_result =
                disk::services::trash_content_internal::ResolveRequiredContentId(
                    trash_item.content_id,
                    trash_item.item_data
                );
            if (!content_id_result.has_value()) {
                LOG_WARN << "Cannot permanently delete trash file without valid content_id: trash_id="
                         << trash_id;
                result.status = "failed";
                result.code = static_cast<uint16_t>(content_id_result.error().code);
                result.message = content_id_result.error().message;
                result.field = "content_id";
                result.value = trash_item.content_id.has_value() ? std::to_string(trash_item.content_id.value()) : "NULL";
                co_return 0;
            }

            if (content_id_result->source ==
                disk::services::trash_content_internal::ContentIdSource::ItemData) {
                LOG_DEBUG << "Resolved legacy trash content_id from item_data during delete: trash_id="
                          << trash_id << ", content_id=" << content_id_result->value;
            }

            auto content_id = content_id_result->value;

            {
                try {
                    auto decrement_result = co_await m_db_client->execSqlCoro(
                        "UPDATE file_contents SET ref_count = GREATEST(ref_count - 1, 0) WHERE id = ?",
                        content_id
                    );

                    if (decrement_result.affectedRows() == 0) {
                        LOG_DEBUG << "File content ref count already 0 or content missing, not decrementing: content_id="
                                  << content_id;
                    } else {
                        CoroMapper<FileContents> content_mapper(m_db_client);
                        auto content = co_await content_mapper.findOne(
                            Criteria(FileContents::Cols::_id, CompareOperator::EQ, content_id)
                        );

                        LOG_DEBUG << "Updated file content ref count: content_id=" << content_id
                                  << ", ref_count=" << content.getValueOfRefCount();

                        if (content.getValueOfRefCount() == 0) {
                            auto* storage = disk::storage::StorageMgr::GetStorage();
                            if (storage == nullptr) {
                                LOG_WARN << "Storage manager is not initialized, skip blob cleanup: content_id="
                                         << content_id << ", storage_path=" << content.getValueOfStoragePath();
                            } else {
                                auto delete_result =
                                    co_await storage->DeletePath(content.getValueOfStoragePath());
                                if (!delete_result.has_value()) {
                                    LOG_WARN << "Failed to cleanup blob after ref_count reached zero: content_id="
                                             << content_id
                                             << ", storage_path=" << content.getValueOfStoragePath()
                                             << ", error_code="
                                             << static_cast<uint32_t>(delete_result.error().code)
                                             << ", error_message=" << delete_result.error().message;
                                } else {
                                    LOG_INFO
                                        << "Blob cleanup completed after ref_count reached zero: content_id="
                                        << content_id
                                        << ", storage_path=" << content.getValueOfStoragePath();
                                }
                            }
                        }
                    }
                } catch (const drogon::orm::DrogonDbException& e) {
                    LOG_WARN << "Failed to update file content ref count: content_id="
                             << content_id;
                }
            }

            CoroMapper<Trash> trash_mapper(m_db_client);
            co_await trash_mapper.deleteByPrimaryKey(trash_id);

            result.status = "success";
            result.freed_space = item_size;

            LOG_INFO << "File permanently deleted: trash_id=" << trash_id
                     << ", freed_space=" << item_size;

            co_return item_size;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to permanently delete file: trash_id=" << trash_item.id << " - "
                      << e.base().what();
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

            PrefetchedTrashItem trash_item;
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
            LOG_ERROR << "Failed to permanently delete folder: trash_id=" << trash_id << " - "
                      << e.base().what();
            result.status = "failed";
            result.code = static_cast<uint16_t>(ErrorCode::InternalError);
            result.message = "Failed to permanently delete folder";
            co_return 0;
        }
    }

    auto TrashService::DeleteFolder(
        const PrefetchedTrashItem& trash_item,
        uint64_t user_id,
        BatchResultItem& result
    ) -> drogon::Task<uint64_t> {

        try {
            auto trash_id = trash_item.id;
            auto item_size = trash_item.item_size;

            CoroMapper<Trash> trash_mapper(m_db_client);
            co_await trash_mapper.deleteByPrimaryKey(trash_id);

            result.status = "success";
            result.freed_space = item_size;

            LOG_INFO << "Folder permanently deleted: trash_id=" << trash_id
                     << ", freed_space=" << item_size;

            co_return item_size;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to permanently delete folder: trash_id=" << trash_item.id << " - "
                      << e.base().what();
            result.status = "failed";
            result.code = static_cast<uint16_t>(ErrorCode::InternalError);
            result.message = "Failed to permanently delete folder";
            co_return 0;
        }
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
                LOG_WARN << "Unable to generate unique filename, max attempts reached: " << name;
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
            LOG_ERROR << "Failed to check filename: " << e.base().what();
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
            LOG_ERROR << "Failed to check folder name: " << e.base().what();
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
            LOG_DEBUG << "Folder not found: folder_id=" << folder_id;
            co_return false;
        }
    }

    auto TrashService::UpdateStorageUsed(uint64_t user_id, int64_t delta) -> drogon::Task<void> {
        try {
            CoroMapper<Users> mapper(m_db_client);
            auto user =
                co_await mapper.findOne(Criteria(Users::Cols::_id, CompareOperator::EQ, user_id));

            auto new_used = static_cast<int64_t>(user.getValueOfStorageUsed()) + delta;
            if (new_used < 0) {
                new_used = 0;
            }

            user.setStorageUsed(static_cast<uint64_t>(new_used));
            co_await mapper.update(user);

            LOG_DEBUG << "Storage usage updated: user_id=" << user_id << ", delta=" << delta
                      << ", new_used=" << new_used;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to update storage usage: " << e.base().what();
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
