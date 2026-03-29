/**
 * @file CleanupService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 系统清理服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "CleanupService.hpp"

#include <drogon/orm/CoroMapper.h>
#include <drogon/orm/Criteria.h>

#include "models/FileContents.hpp"
#include "models/Trash.hpp"
#include "models/UploadTasks.hpp"
#include "models/Users.hpp"
#include "storage/StorageMgr.hpp"

namespace disk::services {

    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::FileContents;
    using drogon_model::disk::Trash;
    using drogon_model::disk::UploadTasks;
    using drogon_model::disk::Users;

    constexpr int kUploadTaskCleanupBatchSize = 100;

    CleanupService::CleanupService(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
        LOG_DEBUG << "CleanupService initialization completed";
    }

    auto CleanupService::CleanupExpiredTrash() -> drogon::Task<Result<int>> {
        LOG_INFO << "Starting cleanup of expired trash items";

        try {
            auto result = co_await m_db_client->execSqlCoro(
                "SELECT id, user_id, item_type, item_size, content_id, item_data FROM trash " "WHERE " "exp" "i" "r" "e" "s" "_at < " "NOW()"
            );

            int deleted_count = 0;
            std::unordered_map<uint64_t, int64_t> user_storage_delta;

            for (size_t i = 0; i < result.size(); ++i) {
                const auto& row = result[i];
                auto trash_id = row["id"].as<uint64_t>();
                auto user_id = row["user_id"].as<uint64_t>();
                auto item_type = row["item_type"].as<std::string>();
                auto item_size = row["item_size"].as<uint64_t>();
                auto item_data_str = row["item_data"].as<std::string>();

                if (item_type == "file") {
                    uint64_t content_id = 0;
                    bool has_content_id = false;
                    if (!row["content_id"].isNull()) {
                        content_id = row["content_id"].as<uint64_t>();
                        has_content_id = true;
                    } else {
                        Json::Value item_data;
                        Json::Reader reader;
                        if (reader.parse(item_data_str, item_data) &&
                            item_data.isMember("content_id")) {
                            content_id = item_data["content_id"].asUInt64();
                            has_content_id = true;
                        }
                    }
                    if (has_content_id) {
                        co_await DecrementContentRefCount(content_id);
                    }
                }

                co_await m_db_client->execSqlCoro("DELETE FROM trash WHERE id = ?", trash_id);

                user_storage_delta[user_id] -= static_cast<int64_t>(item_size);
                deleted_count++;

                LOG_DEBUG << "Cleaned up trash item: trash_id=" << trash_id
                          << ", user_id=" << user_id << ", size=" << item_size;
            }

            for (const auto& [user_id, delta] : user_storage_delta) {
                if (delta != 0) {
                    co_await UpdateStorageUsed(user_id, delta);
                }
            }

            LOG_INFO << "Trash cleanup completed: deleted_count=" << deleted_count;
            co_return deleted_count;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Database error cleaning expired trash: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to clean expired trash")
            );
        } catch (const std::exception& e) {
            LOG_ERROR << "Unknown error cleaning expired trash: " << e.what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to clean expired trash")
            );
        }
    }

    auto CleanupService::CleanupExpiredUploadTasks() -> drogon::Task<Result<int>> {
        LOG_INFO << "Starting cleanup of expired upload tasks";

        try {
            auto result = co_await m_db_client->execSqlCoro(
                "SELECT id, temp_path, user_id, reserved_bytes FROM upload_tasks " "WHERE status = 0 AND expires_at < NOW() " "LIMIT ?",
                kUploadTaskCleanupBatchSize
            );

            int cleaned_count = 0;
            auto* storage = disk::storage::StorageMgr::GetStorage();
            std::unordered_map<uint64_t, uint64_t> user_reserved_delta;

            for (size_t i = 0; i < result.size(); ++i) {
                const auto& row = result[i];
                auto task_id = row["id"].as<std::string>();
                auto temp_path = row["temp_path"].as<std::string>();
                auto user_id = row["user_id"].as<uint64_t>();
                auto reserved_bytes = row["reserved_bytes"].as<uint64_t>();

                if (storage != nullptr) {
                    auto delete_result = co_await storage->DeletePath(temp_path);
                    if (!delete_result.has_value()) {
                        LOG_WARN << "Failed to cleanup temp file for expired upload task: task_id="
                                 << task_id << ", temp_path=" << temp_path
                                 << ", error_code=" << static_cast<uint32_t>(delete_result.error().code)
                                 << ", error_message=" << delete_result.error().message;
                    } else {
                        LOG_DEBUG << "Temp file cleaned for expired upload task: task_id="
                                  << task_id << ", temp_path=" << temp_path;
                    }
                }

                co_await m_db_client->execSqlCoro(
                    "UPDATE upload_tasks SET status = 3, finalized_at = NOW(), fail_reason = '任务过期' " "WHERE id = ? AND status = 0",
                    task_id
                );

                if (reserved_bytes > 0) {
                    user_reserved_delta[user_id] += reserved_bytes;
                }

                cleaned_count++;

                LOG_DEBUG << "Expired upload task marked as expired: task_id=" << task_id
                          << ", user_id=" << user_id << ", reserved_bytes=" << reserved_bytes;
            }

            for (const auto& [user_id, delta] : user_reserved_delta) {
                co_await m_db_client->execSqlCoro(
                    "UPDATE users SET storage_reserved = GREATEST(storage_reserved - ?, 0) WHERE id = ?",
                    delta,
                    user_id
                );
                LOG_DEBUG << "Released reserved storage for expired upload tasks: user_id=" << user_id
                          << ", released_bytes=" << delta;
            }

            LOG_INFO << "Upload task cleanup completed: cleaned_count=" << cleaned_count;
            co_return cleaned_count;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Database error cleaning expired upload tasks: " << e.base().what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to clean expired upload tasks")
            );
        } catch (const std::exception& e) {
            LOG_ERROR << "Unknown error cleaning expired upload tasks: " << e.what();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to clean expired upload tasks")
            );
        }
    }

    auto CleanupService::UpdateStorageUsed(uint64_t user_id, int64_t delta) -> drogon::Task<void> {
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
            LOG_ERROR << "Failed to update storage usage: user_id=" << user_id << " - "
                      << e.base().what();
        }
    }

    auto CleanupService::DecrementContentRefCount(uint64_t content_id) -> drogon::Task<void> {
        try {
            auto decrement_result = co_await m_db_client->execSqlCoro(
                "UPDATE file_contents SET ref_count = GREATEST(ref_count - 1, 0) WHERE id = ?",
                content_id
            );

            if (decrement_result.affectedRows() == 0) {
                LOG_DEBUG << "File content reference count already 0 or content missing, skip decrement: content_id="
                          << content_id;
                co_return;
            }

            CoroMapper<FileContents> mapper(m_db_client);
            auto content = co_await mapper.findOne(
                Criteria(FileContents::Cols::_id, CompareOperator::EQ, content_id)
            );

            LOG_DEBUG << "File content reference count decremented: content_id=" << content_id
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
            LOG_WARN << "Failed to update file content reference count: content_id=" << content_id
                     << " - " << e.base().what();
        }
    }

} // namespace disk::services
