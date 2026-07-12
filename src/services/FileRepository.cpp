/**
 * @file FileRepository.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件持久化原语实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "FileRepository.hpp"

#include <utility>

#include "utils/BatchUtils.hpp"

namespace disk::file {

    using disk::utils::BatchUtils;
    using disk::utils::DEFAULT_BATCH_CHUNK_SIZE;
    using drogon_model::disk::Files;

    namespace {
        constexpr auto kSelectOwnedFileSql =
            "SELECT id, user_id, folder_id, content_id, name, extension, size, mime_type, path, "
            "is_favorite, download_count, last_accessed_at, created_at, updated_at "
            "FROM files WHERE id = $1 AND user_id = $2";

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

        constexpr auto kUpdateDescendantFilePathsForFolderMoveSql =
            "UPDATE files SET path = $1 || SUBSTRING(path FROM LENGTH($2) + 1), updated_at = $3 "
            "WHERE user_id = $4 AND path LIKE $2 || '%'";
    } ///< namespace

    FileRepository::FileRepository(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
    }

    auto FileRepository::FindOwnedFile(
        const drogon::orm::DbClientPtr& client,
        uint64_t file_id,
        uint64_t user_id
    ) const -> drogon::Task<std::optional<Files>> {
        auto result = co_await client->execSqlCoro(kSelectOwnedFileSql, file_id, user_id);
        if (result.empty()) {
            co_return std::nullopt;
        }
        co_return Files(result[0], -1);
    }

    auto FileRepository::FetchOwnedFilesByIds(
        const drogon::orm::DbClientPtr& client,
        const std::vector<uint64_t>& file_ids,
        uint64_t user_id
    ) const -> drogon::Task<std::vector<Files>> {
        std::vector<Files> files;
        if (file_ids.empty()) {
            co_return files;
        }

        auto chunks = BatchUtils::Chunk(file_ids, DEFAULT_BATCH_CHUNK_SIZE);
        for (const auto& chunk : chunks) {
            if (chunk.empty()) {
                continue;
            }
            auto result = co_await client->execSqlCoro(
                std::string(kSelectOwnedFilesPrefixSql) + BatchUtils::BuildSafeNumericInClause(chunk) +
                    ") AND user_id = $1 ORDER BY id ASC",
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

    auto FileRepository::UpdateDescendantFilePathsForFolderMove(
        const drogon::orm::DbClientPtr& client,
        uint64_t user_id,
        const std::string& old_folder_path_prefix,
        const std::string& new_folder_path_prefix,
        const trantor::Date& updated_at
    ) const -> drogon::Task<uint64_t> {
        auto result = co_await client->execSqlCoro(
            kUpdateDescendantFilePathsForFolderMoveSql,
            new_folder_path_prefix,
            old_folder_path_prefix,
            updated_at,
            user_id
        );
        co_return static_cast<uint64_t>(result.affectedRows());
    }

} ///< namespace disk::file
