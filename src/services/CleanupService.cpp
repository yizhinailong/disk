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
#include "models/Users.hpp"
#include "storage/StorageMgr.hpp"

namespace disk::services {

    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::FileContents;
    using drogon_model::disk::Trash;
    using drogon_model::disk::Users;

    CleanupService::CleanupService(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
        LOG_DEBUG << "CleanupService initialization completed";
    }

    auto CleanupService::CleanupExpiredTrash() -> drogon::Task<Result<int>> {
        LOG_INFO << "Starting cleanup of expired trash items";

        try {
            auto result = co_await m_db_client->execSqlCoro(
                "SELECT id, user_id, item_type, item_size, item_data FROM trash " "WHERE " "exp" "i" "r" "e" "s" "_at < " "NOW()"
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
                    Json::Value item_data;
                    Json::Reader reader;
                    if (reader.parse(item_data_str, item_data) &&
                        item_data.isMember("content_id")) {
                        auto content_id = item_data["content_id"].asUInt64();
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
            CoroMapper<FileContents> mapper(m_db_client);
            auto content = co_await mapper.findOne(
                Criteria(FileContents::Cols::_id, CompareOperator::EQ, content_id)
            );

            auto current_ref_count = content.getValueOfRefCount();
            if (current_ref_count > 0) {
                const auto new_ref_count = current_ref_count - 1;
                content.setRefCount(new_ref_count);
                co_await mapper.update(content);
                LOG_DEBUG << "File content reference count decremented: content_id=" << content_id
                          << ", ref_count=" << new_ref_count;

                if (new_ref_count == 0) {
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
            } else {
                LOG_DEBUG << "File content reference count already 0, skip decrement: content_id="
                          << content_id;
            }
        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_WARN << "Failed to update file content reference count: content_id=" << content_id
                     << " - " << e.base().what();
        }
    }

} // namespace disk::services
