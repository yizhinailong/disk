/**
 * @file FileRepository.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件持久化原语实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "FileRepository.hpp"

#include "utils/BatchUtils.hpp"

namespace disk::file {

    using disk::utils::BatchUtils;
    using disk::utils::DEFAULT_BATCH_CHUNK_SIZE;
    using drogon_model::disk::Files;

    namespace {
        constexpr auto kSelectOwnedFileForUpdateSql =
            "SELECT id, user_id, folder_id, content_id, name, extension, size, mime_type, path, "
            "is_favorite, download_count, last_accessed_at, created_at, updated_at "
            "FROM files WHERE id = $1 AND user_id = $2 FOR UPDATE";

        constexpr auto kAcquireNameLockSql =
            "SELECT pg_advisory_xact_lock(hashtextextended($1, 0))";

        constexpr auto kNameExistsExcludingSql =
            "SELECT 1 FROM files "
            "WHERE user_id = $1 AND folder_id = $2 AND name = $3 AND id <> $4 LIMIT 1";

        constexpr auto kSelectOwnedFilesPrefixSql =
            "SELECT id, user_id, folder_id, content_id, name, extension, size, mime_type, path, "
            "is_favorite, download_count, last_accessed_at, created_at, updated_at "
            "FROM files WHERE id IN (";

        constexpr auto kSelectFilesInFoldersPrefixSql =
            "SELECT id, user_id, folder_id, content_id, name, extension, size, mime_type, path, "
            "is_favorite, download_count, last_accessed_at, created_at, updated_at "
            "FROM files WHERE user_id = $1 AND folder_id IN (";

        constexpr auto kRenameOwnedFileSql =
            "UPDATE files SET name = $1, extension = $2, path = $3, updated_at = $4 "
            "WHERE id = $5 AND user_id = $6";

        constexpr auto kUpdateFileLocationSql =
            "UPDATE files SET folder_id = $1, path = $2, updated_at = $3 "
            "WHERE id = $4 AND user_id = $5";

        constexpr auto kUpdateFilePathSql =
            "UPDATE files SET path = $1, updated_at = $2 WHERE id = $3 AND user_id = $4";

    } ///< namespace

    auto FileRepository::FindOwnedFileForUpdate(
        const drogon::orm::DbClientPtr& client,
        uint64_t file_id,
        uint64_t user_id
    ) const -> drogon::Task<std::optional<Files>> {
        auto result = co_await client->execSqlCoro(
            kSelectOwnedFileForUpdateSql,
            file_id,
            user_id
        );
        if (result.empty()) {
            co_return std::nullopt;
        }
        co_return Files(result[0], -1);
    }

    auto FileRepository::AcquireNameLock(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        uint64_t folder_id,
        const std::string& name
    ) const -> drogon::Task<void> {
        const auto lock_key = "file-name:" + std::to_string(user_id) + ":" +
                              std::to_string(folder_id) + ":" + name;
        co_await client->execSqlCoro(kAcquireNameLockSql, lock_key);
    }

    auto FileRepository::NameExistsExcluding(
        const drogon::orm::DbClientPtr& client,
        const std::string& name,
        uint64_t folder_id,
        uint64_t user_id,
        uint64_t excluded_file_id
    ) const -> drogon::Task<bool> {
        auto result = co_await client->execSqlCoro(
            kNameExistsExcludingSql,
            user_id,
            folder_id,
            name,
            excluded_file_id
        );
        co_return !result.empty();
    }

    auto FileRepository::FetchOwnedFilesByIdsForUpdate(
        const drogon::orm::DbClientPtr& client,
        const std::vector<uint64_t>& file_ids,
        uint64_t user_id
    ) const -> drogon::Task<std::vector<Files>> {
        std::vector<Files> files;
        if (file_ids.empty()) {
            co_return files;
        }

        auto sorted_file_ids = file_ids;
        std::sort(sorted_file_ids.begin(), sorted_file_ids.end());
        sorted_file_ids.erase(
            std::unique(sorted_file_ids.begin(), sorted_file_ids.end()),
            sorted_file_ids.end()
        );

        auto chunks = BatchUtils::Chunk(sorted_file_ids, DEFAULT_BATCH_CHUNK_SIZE);
        for (const auto& chunk : chunks) {
            if (chunk.empty()) {
                continue;
            }
            auto result = co_await client->execSqlCoro(
                std::string(kSelectOwnedFilesPrefixSql) + BatchUtils::BuildSafeNumericInClause(chunk) +
                    ") AND user_id = $1 ORDER BY id ASC FOR UPDATE",
                user_id
            );
            files.reserve(files.size() + result.size());
            for (const auto& row : result) {
                files.emplace_back(row, -1);
            }
        }

        co_return files;
    }

    auto FileRepository::FetchFilesInFolders(
        const drogon::orm::DbClientPtr& client,
        const std::vector<uint64_t>& folder_ids,
        uint64_t user_id
    ) const -> drogon::Task<std::vector<Files>> {
        std::vector<Files> files;
        if (folder_ids.empty()) {
            co_return files;
        }

        auto result = co_await client->execSqlCoro(
            std::string(kSelectFilesInFoldersPrefixSql) + BatchUtils::BuildSafeNumericInClause(folder_ids) +
                ") ORDER BY folder_id ASC, id ASC",
            user_id
        );
        files.reserve(result.size());
        for (const auto& row : result) {
            files.emplace_back(row, -1);
        }

        co_return files;
    }

    auto FileRepository::RenameOwnedFile(
        const drogon::orm::DbClientPtr& client,
        uint64_t file_id,
        uint64_t user_id,
        const std::string& new_name,
        const std::string& extension,
        const std::string& new_path,
        const trantor::Date& updated_at
    ) const -> drogon::Task<bool> {
        auto result = co_await client->execSqlCoro(
            kRenameOwnedFileSql,
            new_name,
            extension,
            new_path,
            updated_at,
            file_id,
            user_id
        );
        co_return result.affectedRows() > 0;
    }

    auto FileRepository::UpdateFileLocation(
        const drogon::orm::DbClientPtr& client,
        uint64_t file_id,
        uint64_t user_id,
        uint64_t folder_id,
        const std::string& new_path,
        const trantor::Date& updated_at
    ) const -> drogon::Task<bool> {
        auto result = co_await client->execSqlCoro(
            kUpdateFileLocationSql,
            folder_id,
            new_path,
            updated_at,
            file_id,
            user_id
        );
        co_return result.affectedRows() > 0;
    }

    auto FileRepository::UpdateFilePath(
        const drogon::orm::DbClientPtr& client,
        uint64_t file_id,
        uint64_t user_id,
        const std::string& new_path,
        const trantor::Date& updated_at
    ) const -> drogon::Task<bool> {
        auto result = co_await client->execSqlCoro(
            kUpdateFilePathSql,
            new_path,
            updated_at,
            file_id,
            user_id
        );
        co_return result.affectedRows() > 0;
    }

} ///< namespace disk::file
