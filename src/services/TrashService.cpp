/**
 * @file TrashService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 回收站服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "TrashService.hpp"

#include <drogon/orm/CoroMapper.h>
#include <drogon/orm/Criteria.h>

#include "models/FileContents.hpp"
#include "models/Files.hpp"
#include "models/Folders.hpp"
#include "models/Trash.hpp"
#include "models/Users.hpp"

namespace disk::trash {

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

        for (auto trash_id : trash_ids) {
            BatchResultItem result;
            result.trash_id = trash_id;

            try {
                CoroMapper<Trash> trash_mapper(m_db_client);
                auto trash_item = co_await trash_mapper.findOne(
                    Criteria(Trash::Cols::_id, CompareOperator::EQ, trash_id)
                );

                if (trash_item.getValueOfUserId() != user_id) {
                    result.status = "failed";
                    result.code = static_cast<uint16_t>(ErrorCode::ResourceNotFound);
                    result.message = "Trash item not found";
                    result.field = "trash_id";
                    result.value = std::to_string(trash_id);
                    response.summary.failure_count++;
                    response.results.push_back(result);
                    continue;
                }

                auto item_type = trash_item.getValueOfItemType();
                if (item_type == "file") {
                    co_await RestoreFile(trash_id, user_id, result);
                } else if (item_type == "folder") {
                    co_await RestoreFolder(trash_id, user_id, result);
                } else {
                    result.status = "failed";
                    result.code = static_cast<uint16_t>(ErrorCode::ValidationFailed);
                    result.message = "Unknown item type";
                    response.summary.failure_count++;
                }

            } catch (const drogon::orm::DrogonDbException& e) {
                LOG_WARN << "Restore failed, item not found: trash_id=" << trash_id << " - "
                         << e.base().what();
                result.status = "failed";
                result.code = static_cast<uint16_t>(ErrorCode::ResourceNotFound);
                result.message = "Trash item not found";
                result.field = "trash_id";
                result.value = std::to_string(trash_id);
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

        for (auto trash_id : trash_ids) {
            BatchResultItem result;
            result.trash_id = trash_id;
            uint64_t freed_space = 0;

            try {
                CoroMapper<Trash> trash_mapper(m_db_client);
                auto trash_item = co_await trash_mapper.findOne(
                    Criteria(Trash::Cols::_id, CompareOperator::EQ, trash_id)
                );

                if (trash_item.getValueOfUserId() != user_id) {
                    result.status = "failed";
                    result.code = static_cast<uint16_t>(ErrorCode::ResourceNotFound);
                    result.message = "Trash item not found";
                    result.field = "trash_id";
                    result.value = std::to_string(trash_id);
                    response.summary.failure_count++;
                    response.results.push_back(result);
                    continue;
                }

                auto item_type = trash_item.getValueOfItemType();
                if (item_type == "file") {
                    freed_space = co_await DeleteFile(trash_id, user_id, result);
                } else if (item_type == "folder") {
                    freed_space = co_await DeleteFolder(trash_id, user_id, result);
                } else {
                    result.status = "failed";
                    result.code = static_cast<uint16_t>(ErrorCode::ValidationFailed);
                    result.message = "Unknown item type";
                    response.summary.failure_count++;
                }

            } catch (const drogon::orm::DrogonDbException& e) {
                LOG_WARN << "Delete failed, item not found: trash_id=" << trash_id << " - "
                         << e.base().what();
                result.status = "failed";
                result.code = static_cast<uint16_t>(ErrorCode::ResourceNotFound);
                result.message = "Trash item not found";
                result.field = "trash_id";
                result.value = std::to_string(trash_id);
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
            CoroMapper<Trash> trash_mapper(m_db_client);
            auto trash_items = co_await trash_mapper.findBy(
                Criteria(Trash::Cols::_user_id, CompareOperator::EQ, user_id)
            );

            for (const auto& trash_item : trash_items) {
                auto trash_id = trash_item.getValueOfId();
                auto item_type = trash_item.getValueOfItemType();
                auto item_size = trash_item.getValueOfItemSize();

                BatchResultItem result;
                result.trash_id = trash_id;
                uint64_t freed_space = 0;

                if (item_type == "file") {
                    freed_space = co_await DeleteFile(trash_id, user_id, result);
                } else if (item_type == "folder") {
                    freed_space = co_await DeleteFolder(trash_id, user_id, result);
                }

                if (result.status == "success") {
                    response.deleted_count++;
                    response.freed_space += freed_space;
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
            auto trash_item = co_await trash_mapper.findOne(
                Criteria(Trash::Cols::_id, CompareOperator::EQ, trash_id)
            );

            auto original_folder_id = trash_item.getValueOfOriginalFolderId();
            auto item_name = trash_item.getValueOfItemName();

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

            Json::Value item_data;
            Json::Reader reader;
            reader.parse(trash_item.getValueOfItemData(), item_data);

            std::string file_path = parent_path + final_name;

            Files file;
            file.setUserId(user_id);
            file.setContentId(item_data["content_id"].asUInt64());
            file.setFolderId(target_folder_id);
            file.setName(final_name);
            file.setExtension(ExtractExtension(final_name));
            file.setSize(trash_item.getValueOfItemSize());
            file.setMimeType(item_data.get("mime_type", "application/octet-stream").asString());
            file.setPath(file_path);
            file.setIsFavorite(false);
            file.setDownloadCount(0);
            file.setCreatedAt(trantor::Date::now());
            file.setUpdatedAt(trantor::Date::now());

            CoroMapper<Files> file_mapper(m_db_client);
            auto inserted_file = co_await file_mapper.insert(file);

            co_await trash_mapper.deleteByPrimaryKey(trash_id);

            result.status = "success";
            result.file_id = inserted_file.getValueOfId();
            result.path = file_path;

            LOG_INFO << "File restored successfully: trash_id=" << trash_id
                     << ", file_id=" << inserted_file.getValueOfId() << ", name=" << final_name
                     << ", path=" << file_path;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to restore file: trash_id=" << trash_id << " - "
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
            auto trash_item = co_await trash_mapper.findOne(
                Criteria(Trash::Cols::_id, CompareOperator::EQ, trash_id)
            );

            auto original_folder_id = trash_item.getValueOfOriginalFolderId();
            auto item_name = trash_item.getValueOfItemName();

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

            co_await trash_mapper.deleteByPrimaryKey(trash_id);

            result.status = "success";
            result.folder_id = inserted_folder.getValueOfId();
            result.path = folder_path;

            LOG_INFO << "Folder restored successfully: trash_id=" << trash_id
                     << ", folder_id=" << inserted_folder.getValueOfId() << ", name=" << final_name
                     << ", path=" << folder_path << ", depth=" << folder_depth;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to restore folder: trash_id=" << trash_id << " - "
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
            auto trash_item = co_await trash_mapper.findOne(
                Criteria(Trash::Cols::_id, CompareOperator::EQ, trash_id)
            );

            auto item_size = trash_item.getValueOfItemSize();

            Json::Value item_data;
            Json::Reader reader;
            reader.parse(trash_item.getValueOfItemData(), item_data);

            if (item_data.isMember("content_id")) {
                auto content_id = item_data["content_id"].asUInt64();
                try {
                    CoroMapper<FileContents> content_mapper(m_db_client);
                    auto content = co_await content_mapper.findOne(
                        Criteria(FileContents::Cols::_id, CompareOperator::EQ, content_id)
                    );
                    auto current_ref_count = content.getValueOfRefCount();
                    if (current_ref_count > 0) {
                        content.setRefCount(current_ref_count - 1);
                        co_await content_mapper.update(content);
                        LOG_DEBUG << "Updated file content ref count: content_id=" << content_id
                                  << ", ref_count=" << (current_ref_count - 1);
                    } else {
                        LOG_DEBUG
                            << "File content ref count already 0, not decrementing: content_id="
                            << content_id;
                    }
                } catch (const drogon::orm::DrogonDbException& e) {
                    LOG_WARN << "Failed to update file content ref count: content_id="
                             << content_id;
                }
            }

            co_await trash_mapper.deleteByPrimaryKey(trash_id);

            result.status = "success";
            result.freed_space = item_size;

            LOG_INFO << "File permanently deleted: trash_id=" << trash_id
                     << ", freed_space=" << item_size;

            co_return item_size;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to permanently delete file: trash_id=" << trash_id << " - "
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
            auto trash_item = co_await trash_mapper.findOne(
                Criteria(Trash::Cols::_id, CompareOperator::EQ, trash_id)
            );

            auto item_size = trash_item.getValueOfItemSize();

            co_await trash_mapper.deleteByPrimaryKey(trash_id);

            result.status = "success";
            result.freed_space = item_size;

            LOG_INFO << "Folder permanently deleted: trash_id=" << trash_id
                     << ", freed_space=" << item_size;

            co_return item_size;

        } catch (const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "Failed to permanently delete folder: trash_id=" << trash_id << " - "
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
