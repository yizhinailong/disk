/**
 * @file CleanupService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 系统清理服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "CleanupService.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <drogon/orm/CoroMapper.h>
#include <drogon/orm/Criteria.h>

#include "models/FileContents.hpp"
#include "models/Trash.hpp"
#include "models/UploadTasks.hpp"
#include "models/Users.hpp"
#include "services/TrashContentIdResolver.hpp"
#include "storage/StorageMgr.hpp"
#include "utils/BatchUtils.hpp"

namespace disk::services {

    using disk::utils::BatchUtils;
    using disk::utils::DEFAULT_BATCH_CHUNK_SIZE;
    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::FileContents;

    constexpr int kUploadTaskCleanupBatchSize = 100;
    constexpr int kTrashFetchBatchSize = 100;
    constexpr int kMaxTrashBatchesPerRun = 20;

    CleanupService::CleanupService(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
        Logger::Debug() << "CleanupService initialization completed";
    }

    auto CleanupService::CleanupExpiredTrash() -> drogon::Task<Result<int>> {
        Logger::Info() << "Starting cleanup of expired trash items";

        struct ExpiredTrashItem {
            uint64_t id;
            uint64_t user_id;
            std::string item_type;
            uint64_t item_size;
            std::optional<uint64_t> content_id;
            std::string item_data;
        };

        try {
            int deleted_count = 0;
            std::unordered_map<uint64_t, int64_t> user_storage_delta;
            uint64_t last_seen_id = 0;
            int batch_iteration = 0;

            while (batch_iteration < kMaxTrashBatchesPerRun) {
                auto batch_start = std::chrono::steady_clock::now();
                auto result = co_await m_db_client->execSqlCoro(
                    "SELECT id, user_id, item_type, item_size, content_id, item_data "
                    "FROM trash "
                    "WHERE expires_at < NOW() AND id > $1 "
                    "ORDER BY id ASC "
                    "LIMIT $2",
                    last_seen_id,
                    kTrashFetchBatchSize
                );

                if (result.empty()) {
                    break;
                }

                uint64_t batch_max_id = 0;
                for (const auto& row : result) {
                    uint64_t id = row["id"].as<uint64_t>();
                    if (id > batch_max_id) {
                        batch_max_id = id;
                    }
                }

                std::vector<ExpiredTrashItem> trash_items;
                trash_items.reserve(result.size());
                for (const auto& row : result) {
                    ExpiredTrashItem item{
                        .id = row["id"].as<uint64_t>(),
                        .user_id = row["user_id"].as<uint64_t>(),
                        .item_type = row["item_type"].as<std::string>(),
                        .item_size = row["item_size"].as<uint64_t>(),
                        .item_data = row["item_data"].as<std::string>()
                    };
                    if (!row["content_id"].isNull()) {
                        item.content_id = row["content_id"].as<uint64_t>();
                    }
                    trash_items.push_back(std::move(item));
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

                    std::shared_ptr<drogon::orm::Transaction> transaction;
                    std::vector<std::string> zero_ref_paths;
                    std::vector<uint64_t> unique_content_ids;
                    std::unordered_map<uint64_t, int64_t> chunk_user_storage_delta;

                    try {
                        transaction = co_await m_db_client->newTransactionCoro();

                        std::vector<uint64_t> trash_ids;
                        trash_ids.reserve(chunk.size());

                        std::vector<uint64_t> content_ids;
                        content_ids.reserve(chunk.size());

                        for (const auto& item : chunk) {
                            if (item.item_type == "file") {
                                auto content_id_result =
                                    trash_content_internal::ResolveRequiredContentId(
                                        item.content_id,
                                        item.item_data
                                    );
                                if (!content_id_result.has_value()) {
                                    Logger::Warn() << "Skip expired trash cleanup for legacy row without valid content_id: trash_id="
                                             << item.id << ", user_id=" << item.user_id;
                                    continue;
                                }

                                if (content_id_result->source ==
                                    trash_content_internal::ContentIdSource::ItemData) {
                                    Logger::Debug() << "Resolved legacy trash content_id from item_data during cleanup: trash_id="
                                              << item.id << ", content_id=" << content_id_result->value;
                                }

                                content_ids.push_back(content_id_result->value);
                            }

                            trash_ids.push_back(item.id);
                            chunk_user_storage_delta[item.user_id] -= static_cast<int64_t>(item.item_size);
                        }

                        if (trash_ids.empty()) {
                            continue;
                        }

                        if (!content_ids.empty()) {
                            std::unordered_map<uint64_t, int> content_id_counts;
                            content_id_counts.reserve(content_ids.size());
                            for (const auto& id : content_ids) {
                                content_id_counts[id]++;
                            }

                            unique_content_ids.reserve(content_id_counts.size());

                            std::string update_sql = "UPDATE file_contents SET ref_count = GREATEST(ref_count - CASE id ";
                            for (const auto& [id, count] : content_id_counts) {
                                unique_content_ids.push_back(id);
                                update_sql += "WHEN " + std::to_string(id) + " THEN " +
                                              std::to_string(count) + " ";
                            }
                            update_sql += "ELSE 0 END, 0) WHERE id IN (" +
                                          BatchUtils::BuildSafeNumericInClause(unique_content_ids) +
                                          ")";
                            co_await transaction->execSqlCoro(update_sql);

                            auto content_in_clause = BatchUtils::BuildSafeNumericInClause(unique_content_ids);

                            auto zero_ref_rows = co_await transaction->execSqlCoro(
                                "SELECT id, storage_path FROM file_contents " "WHERE ref_count = 0 AND id IN (" + content_in_clause + ")"
                            );

                            zero_ref_paths.reserve(zero_ref_rows.size());
                            for (const auto& row : zero_ref_rows) {
                                zero_ref_paths.push_back(row["storage_path"].as<std::string>());
                            }
                        }

                        auto delete_result = co_await transaction->execSqlCoro(
                            "DELETE FROM trash WHERE id IN (" + BatchUtils::BuildSafeNumericInClause(trash_ids) +
                            ")"
                        );

                        if (delete_result.affectedRows() != trash_ids.size()) {
                            throw std::runtime_error("Chunk delete affected rows mismatch");
                        }

                        deleted_count += static_cast<int>(trash_ids.size());
                        for (const auto& [user_id, delta] : chunk_user_storage_delta) {
                            user_storage_delta[user_id] += delta;
                        }
                    } catch (const std::exception& e) {
                        if (transaction) {
                            try {
                                transaction->rollback();
                            } catch (const std::exception& rollback_error) {
                                Logger::Error() << "Rollback failed for expired trash cleanup chunk: "
                                          << rollback_error.what();
                            }
                        }
                        Logger::Error() << "Failed to cleanup expired trash chunk atomically: " << e.what();
                        chunks_failed++;
                        continue;
                    }

                    chunks_succeeded++;

                    if (!zero_ref_paths.empty() && !unique_content_ids.empty()) {
                        try {
                            auto verified_rows = co_await m_db_client->execSqlCoro(
                                "SELECT id, storage_path FROM file_contents " "WHERE ref_count = 0 AND id IN (" +
                                BatchUtils::BuildSafeNumericInClause(unique_content_ids) + ")"
                            );

                            blobs_verified += static_cast<int>(verified_rows.size());

                            if (verified_rows.size() < zero_ref_paths.size()) {
                                Logger::Info() << "[cleanup_batch] blob safety check: candidates="
                                         << zero_ref_paths.size()
                                         << " verified=" << verified_rows.size()
                                         << " reclaimed_by_concurrent="
                                         << (zero_ref_paths.size() - verified_rows.size());
                            }

                            auto* storage = disk::storage::StorageMgr::GetStorage();
                            if (storage == nullptr) {
                                Logger::Warn() << "Storage manager is not initialized, skip expired-trash blob cleanup for chunk: blob_count="
                                         << verified_rows.size();
                            } else {
                                for (const auto& row : verified_rows) {
                                    auto path = row["storage_path"].as<std::string>();
                                    auto blob_delete_result = co_await storage->DeletePath(path);
                                    if (!blob_delete_result.has_value()) {
                                        Logger::Warn() << "Failed to cleanup expired-trash blob after chunk: storage_path="
                                                 << path << ", error_code="
                                                 << static_cast<uint32_t>(blob_delete_result.error().code)
                                                 << ", error_message=" << blob_delete_result.error().message;
                                    } else {
                                        blobs_deleted++;
                                        Logger::Info() << "Expired-trash blob cleanup completed after chunk: storage_path="
                                                 << path;
                                    }
                                }
                            }
                        } catch (const std::exception& e) {
                            Logger::Error() << "[cleanup_batch] blob re-verification failed, skipping blob deletion for chunk: "
                                      << e.what();
                        }
                    }
                }

                Logger::Info() << "[cleanup_batch] trash batch_iteration="
                         << batch_iteration
                         << " fetch_size=" << result.size()
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

                if (result.size() < static_cast<size_t>(kTrashFetchBatchSize)) {
                    break;
                }
            }

            if (batch_iteration >= kMaxTrashBatchesPerRun) {
                Logger::Info() << "[cleanup_batch] trash reached max batches per run cap: max="
                         << kMaxTrashBatchesPerRun << " rows_deleted=" << deleted_count;
            }

            for (const auto& [user_id, delta] : user_storage_delta) {
                if (delta != 0) {
                    co_await UpdateStorageUsed(user_id, delta);
                }
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

    auto CleanupService::CleanupExpiredUploadTasks() -> drogon::Task<Result<int>> {
        Logger::Info() << "Starting cleanup of expired upload tasks";

        auto batch_start = std::chrono::steady_clock::now();
        try {
            auto result = co_await m_db_client->execSqlCoro(
                "SELECT id, temp_path, user_id, reserved_bytes FROM upload_tasks "
                "WHERE status = 0 AND expires_at < NOW() "
                "LIMIT $1",
                kUploadTaskCleanupBatchSize
            );

            int cleaned_count = 0;
            auto* storage = disk::storage::StorageMgr::GetStorage();
            std::unordered_map<uint64_t, uint64_t> user_reserved_delta;
            std::vector<std::string> expired_task_ids;
            expired_task_ids.reserve(result.size());

            for (const auto& row : result) {
                auto task_id = row["id"].as<std::string>();
                auto temp_path = row["temp_path"].as<std::string>();
                auto user_id = row["user_id"].as<uint64_t>();
                auto reserved_bytes = row["reserved_bytes"].as<uint64_t>();

                expired_task_ids.push_back(task_id);

                if (storage != nullptr) {
                    auto delete_result = co_await storage->CleanupTemp(task_id);
                    if (!delete_result.has_value()) {
                        Logger::Warn() << "Failed to cleanup temp file for expired upload task: task_id="
                                 << task_id << ", temp_path=" << temp_path
                                 << ", error_code=" << static_cast<uint32_t>(delete_result.error().code)
                                 << ", error_message=" << delete_result.error().message;
                    } else {
                        Logger::Debug() << "Temp file cleaned for expired upload task: task_id="
                                  << task_id << ", temp_path=" << temp_path;
                    }
                }

                if (reserved_bytes > 0) {
                    user_reserved_delta[user_id] += reserved_bytes;
                }

                cleaned_count++;

                Logger::Debug() << "Expired upload task marked as expired: task_id=" << task_id
                          << ", user_id=" << user_id << ", reserved_bytes=" << reserved_bytes;
            }

            if (!expired_task_ids.empty()) {
                auto placeholders = BatchUtils::BuildInPlaceholders(expired_task_ids);
                co_await m_db_client->execSqlCoro(
                    "UPDATE upload_tasks SET status = 3, finalized_at = NOW(), fail_reason = '任务过期' " "WHERE id IN (" + placeholders + ") AND status = 0",
                    std::as_const(expired_task_ids)
                );
            }

            for (const auto& [user_id, delta] : user_reserved_delta) {
                co_await m_db_client->execSqlCoro(
                    "UPDATE users SET storage_reserved = GREATEST(storage_reserved - $1, 0) WHERE id = $2",
                    delta,
                    user_id
                );
                Logger::Debug() << "Released reserved storage for expired upload tasks: user_id=" << user_id
                          << ", released_bytes=" << delta;
            }

            Logger::Info() << "[cleanup_batch] upload_tasks batch_size="
                     << result.size()
                     << " cleaned_count=" << cleaned_count
                     << " batch_duration_ms="
                     << std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - batch_start
                        )
                            .count();

            Logger::Info() << "Upload task cleanup completed: cleaned_count=" << cleaned_count;
            co_return cleaned_count;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Database error cleaning expired upload tasks: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to clean expired upload tasks")
            );
        } catch (const std::exception& e) {
            Logger::Error() << "Unknown error cleaning expired upload tasks: " << e.what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to clean expired upload tasks")
            );
        }
    }

    auto CleanupService::UpdateStorageUsed(uint64_t user_id, int64_t delta) -> drogon::Task<void> {
        try {
            co_await m_db_client->execSqlCoro(
                "UPDATE users SET storage_used = GREATEST(CAST(storage_used AS BIGINT) + $1, 0) "
                "WHERE id = $2",
                delta,
                user_id
            );

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Error() << "Failed to update storage usage: user_id=" << user_id << " - "
                      << e.base().what();
        }
    }

    auto CleanupService::DecrementContentRefCount(uint64_t content_id) -> drogon::Task<void> {
        try {
            auto decrement_result = co_await m_db_client->execSqlCoro(
                "UPDATE file_contents SET ref_count = GREATEST(ref_count - 1, 0) WHERE id = $1",
                content_id
            );

            if (decrement_result.affectedRows() == 0) {
                Logger::Debug() << "File content reference count already 0 or content missing, skip decrement: content_id="
                          << content_id;
                co_return;
            }

            CoroMapper<FileContents> mapper(m_db_client);
            auto content = co_await mapper.findOne(
                Criteria(FileContents::Cols::_id, CompareOperator::EQ, content_id)
            );

            Logger::Debug() << "File content reference count decremented: content_id=" << content_id
                      << ", ref_count=" << content.getValueOfRefCount();

            if (content.getValueOfRefCount() == 0) {
                auto* storage = disk::storage::StorageMgr::GetStorage();
                if (storage == nullptr) {
                    LOG_WARN
                        << "Storage manager is not initialized, skip expired-trash blob cleanup: content_id="
                        << content_id << ", storage_path=" << content.getValueOfStoragePath();
                } else {
                    auto delete_result = co_await storage->DeletePath(content.getValueOfStoragePath());
                    if (!delete_result.has_value()) {
                        LOG_WARN
                            << "Failed to cleanup expired-trash blob after ref_count reached zero: content_id="
                            << content_id << ", storage_path=" << content.getValueOfStoragePath()
                            << ", error_code=" << static_cast<uint32_t>(delete_result.error().code)
                            << ", error_message=" << delete_result.error().message;
                    } else {
                        LOG_INFO
                            << "Expired-trash blob cleanup completed after ref_count reached zero: content_id="
                            << content_id << ", storage_path=" << content.getValueOfStoragePath();
                    }
                }
            }
        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "Failed to update file content reference count: content_id=" << content_id
                     << " - " << e.base().what();
        }
    }

} // namespace disk::services
