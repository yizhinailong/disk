/**
 * @file FileQueryService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件查询服务实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "FileQueryService.hpp"

#include <string>

#include <sstream>

#include <json/reader.h>
#include <json/writer.h>

#include "FileServiceUtils.hpp"
#include "models/FileContents.hpp"
#include "models/Files.hpp"
#include "models/Folders.hpp"
#include "utils/RedisKeyPrefix.hpp"

namespace disk::file {

    using drogon::orm::CompareOperator;
    using drogon::orm::CoroMapper;
    using drogon::orm::Criteria;
    using drogon_model::disk::FileContents;
    using drogon_model::disk::Files;
    using drogon_model::disk::Folders;

    /// ==================== 构造函数 ====================

    FileQueryService::FileQueryService(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
        Logger::Debug() << "FileQueryService initialization completed";
    }

    /// ==================== GetFileList ====================

    auto FileQueryService::GetFileList(FileListRequest request, uint64_t user_id)
        -> drogon::Task<Result<FileListResponse>> {

        Logger::Debug() << "Starting get file list: parent_id=" << request.parent_id
                  << ", page=" << request.page << ", page_size=" << request.page_size
                  << ", sort_by=" << request.sort_by << ", sort_order=" << request.sort_order
                  << ", type=" << request.type << ", user_id=" << user_id;

        /// 0. Try Redis cache
        auto cache_key = disk::redis::RedisKeyPrefix::BuildFileListCacheKey(
            user_id, request.parent_id, request.type,
            request.sort_by, request.sort_order, request.page
        );

        auto cache_result = co_await m_redis_service->Get(cache_key);
        if (cache_result.has_value()) {
            Logger::Debug() << "File list cache hit: key=" << cache_key;
            Json::Value cached_json;
            Json::CharReaderBuilder builder;
            std::istringstream stream(*cache_result);
            std::string errors;
            if (Json::parseFromStream(builder, stream, &cached_json, &errors)) {
                co_return FileListResponse::FromJson(cached_json);
            }
            Logger::Warn() << "File list cache parse error: " << errors;
        }
        Logger::Debug() << "File list cache miss: key=" << cache_key;

        /// 1. 验证 parent_id 文件夹存在且属于用户（如果 parent_id != 0）
        if (request.parent_id != 0) {
            try {
                CoroMapper<Folders> folder_mapper(m_db_client);
                auto folder = co_await folder_mapper.findOne(
                    Criteria(Folders::Cols::_id, CompareOperator::EQ, request.parent_id) &&
                    Criteria(Folders::Cols::_user_id, CompareOperator::EQ, user_id)
                );
                Logger::Debug() << "Folder verification passed: folder_id=" << request.parent_id;
            } catch (const drogon::orm::DrogonDbException& e) {
                Logger::Warn() << "Folder not found or no permission: folder_id=" << request.parent_id;
                co_return std::unexpected(ErrorInfo(ErrorCode::FolderNotFound));
            }
        }

        /// 2. 使用 SQL 查询（JOIN 消除 N+1， LIMIT/OFFSET 密除内存分页）
        std::vector<FileListItem> items;
        int total = 0;
        int total_pages = 0;

        const auto order_column = utils::ResolveListSortColumn(request.sort_by, request.type == "folder");
        const std::string order_dir = (request.sort_order == "desc") ? "DESC" : "ASC";
        const std::string inner_order_by =
            utils::BuildDeterministicOrderByClause(order_column, order_dir, request.type == "all");
        const std::string outer_order_by = utils::BuildDeterministicOrderByClause(
            order_column,
            order_dir,
            request.type == "all",
            "page."
        );

        auto offset = (request.page - 1) * request.page_size;

        try {
            if (request.type == "all") {
                auto file_count_result = co_await m_db_client->execSqlCoro(
                    "SELECT COUNT(*) AS cnt FROM files f WHERE f.folder_id = $1 AND f.user_id = $2",
                    request.parent_id,
                    user_id
                );

                if (!file_count_result.empty()) {
                    total += static_cast<int>(file_count_result[0]["cnt"].as<int64_t>());
                }

                auto folder_count_result = co_await m_db_client->execSqlCoro(
                    "SELECT COUNT(*) AS cnt FROM folders fo WHERE fo.parent_id = $1 AND fo.user_id = $2",
                    request.parent_id,
                    user_id
                );

                if (!folder_count_result.empty()) {
                    total += static_cast<int>(folder_count_result[0]["cnt"].as<int64_t>());
                }

                /// 先在窄行结果集上完成分页，再回表补齐详情，避免在宽行 UNION 结果上提前排序。
                const std::string data_sql =
                    "SELECT page.id, page.name, page.type, page.size, " "       COALESCE(f.mime_type, '') AS mime_type, " "       COALESCE(fc.hash_md5, '') AS hash, " "       COALESCE(fo.item_count, 0) AS item_count, " "       page.created_at, page.updated_at " "FROM (" "  SELECT combined.id, combined.name, combined.type, combined.size, combined.created_at, combined.updated_at " "  FROM (" "    SELECT f.id, f.name, 'file' AS type, f.size, f.created_at, f.updated_at " "    FROM files f " "    WHERE f.folder_id = $1 AND f.user_id = $2 " "    UNION ALL " "    SELECT fo.id, fo.name, 'folder' AS type, 0 AS size, fo.created_at, fo.updated_at " "    FROM folders fo " "    WHERE fo.parent_id = $3 AND fo.user_id = $4 " "  ) AS combined " "  ORDER BY " + inner_order_by + " " "  LIMIT $5 OFFSET $6" ") AS page " "LEFT JOIN files f ON page.type = 'file' AND page.id = f.id " "LEFT JOIN folders fo ON page.type = 'folder' AND page.id = fo.id " "LEFT JOIN file_contents fc ON f.content_id = fc.id " "ORDER BY " + outer_order_by;

                auto paginated_result = co_await m_db_client->execSqlCoro(
                    data_sql,
                    request.parent_id,
                    user_id,
                    request.parent_id,
                    user_id,
                    request.page_size,
                    offset
                );

                for (const auto& row : paginated_result) {
                    FileListItem item;
                    item.id = row["id"].as<uint64_t>();
                    item.name = row["name"].as<std::string>();
                    item.type = row["type"].as<std::string>();
                    item.size = row["size"].as<uint64_t>();
                    item.mime_type = row["mime_type"].as<std::string>();
                    item.hash = row["hash"].as<std::string>();
                    item.item_count = static_cast<int>(row["item_count"].as<int64_t>());
                    item.created_at = row["created_at"].as<std::string>();
                    item.updated_at = row["updated_at"].as<std::string>();
                    items.push_back(item);
                }

            } else if (request.type == "file") {
                auto count_result = co_await m_db_client->execSqlCoro(
                    "SELECT COUNT(*) AS cnt FROM files WHERE folder_id = $1 AND user_id = $2",
                    request.parent_id,
                    user_id
                );

                if (!count_result.empty()) {
                    total = static_cast<int>(count_result[0]["cnt"].as<int64_t>());
                }

                const std::string data_sql =
                    "SELECT f.id, f.name, f.size, f.mime_type, " "       COALESCE(fc.hash_md5, '') AS hash, f.created_at, f.updated_at " "FROM (" "  SELECT f.id, f.name, f.size, f.created_at, f.updated_at " "  FROM files f " "  WHERE f.folder_id = $1 AND f.user_id = $2 " "  ORDER BY " + inner_order_by + " " "  LIMIT $3 OFFSET $4" ") AS page " "JOIN files f ON f.id = page.id " "LEFT JOIN file_contents fc ON f.content_id = fc.id " "ORDER BY " + outer_order_by;

                auto paginated_result = co_await m_db_client->execSqlCoro(
                    data_sql,
                    request.parent_id,
                    user_id,
                    request.page_size,
                    offset
                );

                for (const auto& row : paginated_result) {
                    FileListItem item;
                    item.id = row["id"].as<uint64_t>();
                    item.name = row["name"].as<std::string>();
                    item.type = "file";
                    item.size = row["size"].as<uint64_t>();
                    item.mime_type = row["mime_type"].as<std::string>();
                    item.hash = row["hash"].as<std::string>();
                    item.item_count = 0;
                    item.created_at = row["created_at"].as<std::string>();
                    item.updated_at = row["updated_at"].as<std::string>();
                    items.push_back(item);
                }

            } else if (request.type == "folder") {
                auto count_result = co_await m_db_client->execSqlCoro(
                    "SELECT COUNT(*) AS cnt FROM folders WHERE parent_id = $1 AND user_id = $2",
                    request.parent_id,
                    user_id
                );

                if (!count_result.empty()) {
                    total = static_cast<int>(count_result[0]["cnt"].as<int64_t>());
                }

                const std::string data_sql =
                    "SELECT page.id, page.name, page.item_count, page.created_at, page.updated_at " "FROM (" "  SELECT fo.id, fo.name, fo.item_count, fo.created_at, fo.updated_at, 0 AS sort_size " "  FROM folders fo " "  WHERE fo.parent_id = $1 AND fo.user_id = $2 " "  ORDER BY " + inner_order_by + " " "  LIMIT $3 OFFSET $4" ") AS page " "ORDER BY " + outer_order_by;

                auto paginated_result = co_await m_db_client->execSqlCoro(
                    data_sql,
                    request.parent_id,
                    user_id,
                    request.page_size,
                    offset
                );

                for (const auto& row : paginated_result) {
                    FileListItem item;
                    item.id = row["id"].as<uint64_t>();
                    item.name = row["name"].as<std::string>();
                    item.type = "folder";
                    item.size = 0;
                    item.mime_type = "";
                    item.hash = "";
                    item.item_count = static_cast<int>(row["item_count"].as<int64_t>());
                    item.created_at = row["created_at"].as<std::string>();
                    item.updated_at = row["updated_at"].as<std::string>();
                    items.push_back(item);
                }
            }

            total_pages = request.page_size > 0 ? (total + request.page_size - 1) / request.page_size : 0;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "Failed to query file list: " << e.base().what();
        }

        /// 3. 构造响应
        FileListResponse response;
        response.items = items;
        response.pagination = { .page = request.page,
                                .page_size = request.page_size,
                                .total = total,
                                .total_pages = total_pages };

        Logger::Debug() << "File list retrieved successfully: total=" << total
                  << ", page=" << request.page;

        /// Cache the response
        {
            auto response_json = response.ToJson();
            Json::StreamWriterBuilder writer_builder;
            writer_builder["indentation"] = "";
            auto serialized = Json::writeString(writer_builder, response_json);
            co_await m_redis_service->Set(cache_key, serialized, 30);
        }

        co_return response;
    }

    /// ==================== GetFileDetail ====================

    auto FileQueryService::GetFileDetail(uint64_t file_id, uint64_t user_id)
        -> drogon::Task<Result<FileDetailResponse>> {

        Logger::Debug() << "Starting get file detail: file_id=" << file_id << ", user_id=" << user_id;

        try {
            CoroMapper<Files> file_mapper(m_db_client);
            auto file = co_await file_mapper.findOne(
                Criteria(Files::Cols::_id, CompareOperator::EQ, file_id) &&
                Criteria(Files::Cols::_user_id, CompareOperator::EQ, user_id)
            );

            std::string hash;
            std::string mime_type = file.getValueOfMimeType();

            if (file.getContentId()) {
                CoroMapper<FileContents> content_mapper(m_db_client);
                auto content = co_await content_mapper.findOne(
                    Criteria(FileContents::Cols::_id, CompareOperator::EQ, *file.getContentId())
                );
                hash = content.getValueOfHashMd5();
                if (mime_type.empty()) {
                    mime_type = content.getValueOfMimeType();
                }
            }

            FileDetailResponse response;
            response.id = file.getValueOfId();
            response.name = file.getValueOfName();
            response.type = "file";
            response.size = file.getValueOfSize();
            response.hash = hash;
            response.mime_type = mime_type;
            response.parent_id = file.getValueOfFolderId();
            response.path = file.getValueOfPath();
            response.created_at = file.getValueOfCreatedAt().toDbStringLocal();
            response.updated_at = file.getValueOfUpdatedAt().toDbStringLocal();

            Logger::Debug() << "File detail retrieved successfully: name=" << response.name;
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "File not found or no permission: file_id=" << file_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::FileNotFound));
        }
    }

    /// ==================== GetDownloadInfo ====================

    auto FileQueryService::GetDownloadInfo(uint64_t file_id, uint64_t user_id)
        -> drogon::Task<Result<DownloadInfoResponse>> {

        Logger::Debug() << "Starting get download info: file_id=" << file_id << ", user_id=" << user_id;

        /// 1. 查找文件并验证归属
        try {
            CoroMapper<Files> file_mapper(m_db_client);
            auto file = co_await file_mapper.findOne(
                Criteria(Files::Cols::_id, CompareOperator::EQ, file_id) &&
                Criteria(Files::Cols::_user_id, CompareOperator::EQ, user_id)
            );

            /// 2. 获取文件内容信息
            if (!file.getContentId()) {
                Logger::Error() << "File missing content_id: file_id=" << file_id;
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::FileReadError, "File content info missing")
                );
            }

            CoroMapper<FileContents> content_mapper(m_db_client);
            auto content = co_await content_mapper.findOne(
                Criteria(FileContents::Cols::_id, CompareOperator::EQ, *file.getContentId())
            );

            /// 3. 构造响应
            DownloadInfoResponse response;
            response.file_id = file.getValueOfId();
            response.filename = file.getValueOfName();
            response.file_size = file.getValueOfSize();
            response.file_hash = content.getValueOfHashMd5();
            response.mime_type = file.getValueOfMimeType().empty() ? content.getValueOfMimeType() :
                                                                     file.getValueOfMimeType();
            response.supports_range = true;

            Logger::Debug() << "Download info retrieved successfully: filename=" << response.filename
                      << ", size=" << response.file_size;
            co_return response;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "File not found or no permission: file_id=" << file_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::FileNotFound));
        }
    }

    /// ==================== GetDownloadData ====================

    auto FileQueryService::GetDownloadData(uint64_t file_id, uint64_t user_id)
        -> drogon::Task<Result<DownloadInfo>> {

        Logger::Debug() << "Starting get download data: file_id=" << file_id << ", user_id=" << user_id;

        /// 1. 查找文件并验证归属
        try {
            CoroMapper<Files> file_mapper(m_db_client);
            auto file = co_await file_mapper.findOne(
                Criteria(Files::Cols::_id, CompareOperator::EQ, file_id) &&
                Criteria(Files::Cols::_user_id, CompareOperator::EQ, user_id)
            );

            /// 2. 获取文件内容信息
            if (!file.getContentId()) {
                Logger::Error() << "File missing content_id: file_id=" << file_id;
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::FileReadError, "File content info missing")
                );
            }

            CoroMapper<FileContents> content_mapper(m_db_client);
            auto content = co_await content_mapper.findOne(
                Criteria(FileContents::Cols::_id, CompareOperator::EQ, *file.getContentId())
            );

            /// 3. 构造响应
            DownloadInfo info;
            info.file_id = file.getValueOfId();
            info.filename = file.getValueOfName();
            info.file_size = file.getValueOfSize();
            info.file_hash = content.getValueOfHashMd5();
            info.mime_type = file.getValueOfMimeType().empty() ? content.getValueOfMimeType() :
                                                                 file.getValueOfMimeType();
            info.storage_path = content.getValueOfStoragePath();
            info.supports_range = true;

            Logger::Debug() << "Download data retrieved successfully: filename=" << info.filename
                      << ", storage_path=" << info.storage_path;
            co_return info;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "File not found or no permission: file_id=" << file_id;
            co_return std::unexpected(ErrorInfo(ErrorCode::FileNotFound));
        }
    }

    /// ==================== Search ====================

    auto FileQueryService::Search(SearchRequest request, uint64_t user_id)
        -> drogon::Task<Result<SearchResponse>> {

        Logger::Debug() << "Starting search file: keyword=\"" << request.keyword
                  << "\", type=" << request.type << ", folder_id="
                  << (request.folder_id.has_value() ? std::to_string(*request.folder_id) : "null")
                  << ", page=" << request.page << ", page_size=" << request.page_size
                  << ", user_id=" << user_id;

        std::vector<SearchResultItem> items;
        int total = 0;
        int total_pages = 0;

        const bool use_fulltext = utils::IsFulltextEligible(request.keyword);
        const bool has_folder_filter = request.folder_id.has_value();
        const auto normalized_keyword = utils::NormalizeFulltextKeyword(request.keyword);

        /// Escape underscore for LIKE pattern (treat it literally, not as wildcard)
        std::string escaped_like_keyword = request.keyword;
        size_t pos = 0;
        while ((pos = escaped_like_keyword.find('_', pos)) != std::string::npos) {
            escaped_like_keyword.replace(pos, 1, "\\_");
            pos += 2; ///< Skip past the escaped character
        }

        const std::string search_param =
            use_fulltext ? normalized_keyword : "%" + escaped_like_keyword + "%";
        const std::string inner_order_by =
            utils::BuildDeterministicOrderByClause("name", "ASC", request.type == "all");
        const std::string outer_order_by = utils::BuildDeterministicOrderByClause(
            "name",
            "ASC",
            request.type == "all",
            "page."
        );
        auto offset = (request.page - 1) * request.page_size;

        try {
            std::string file_where = use_fulltext ?
                                          "WHERE f.user_id = $1 AND to_tsvector('simple', f.name) @@ to_tsquery('simple', replace($2, ' ', ' | '))" :
                                          "WHERE f.user_id = $1 AND f.name LIKE $2";
            std::string folder_where = use_fulltext ?
                                            "WHERE fo.user_id = $1 AND to_tsvector('simple', fo.name) @@ to_tsquery('simple', replace($2, ' ', ' | '))" :
                                            "WHERE fo.user_id = $1 AND fo.name LIKE $2";

            if (has_folder_filter) {
                file_where += " AND f.folder_id = $3";
                folder_where += " AND fo.parent_id = $3";
            }

            if (request.type == "all") {
                const std::string file_count_sql =
                    "SELECT COUNT(*) AS cnt FROM files f " + file_where;
                const std::string folder_count_sql =
                    "SELECT COUNT(*) AS cnt FROM folders fo " + folder_where;
                const std::string data_sql =
                    "SELECT page.id, page.name, page.type, " "       COALESCE(f.size, 0) AS size, " "       COALESCE(f.mime_type, '') AS mime_type, " "       COALESCE(fc.hash_md5, '') AS hash, " "       COALESCE(fo.item_count, 0) AS item_count, " "       COALESCE(f.path, fo.path) AS path, " "       COALESCE(f.created_at, fo.created_at) AS created_at, " "       COALESCE(f.updated_at, fo.updated_at) AS updated_at " "FROM (" "  SELECT combined.id, combined.name, combined.type " "  FROM (" "    SELECT f.id, f.name, 'file' AS type " "    FROM files f " + file_where + " " "    UNION ALL " "    SELECT fo.id, fo.name, 'folder' AS type " "    FROM folders fo " + folder_where + " " "  ) AS combined " "  ORDER BY " + inner_order_by + " " "  LIMIT $3 OFFSET $4" ") AS page " "LEFT JOIN files f ON page.type = 'file' AND f.id = page.id " "LEFT JOIN folders fo ON page.type = 'folder' AND fo.id = page.id " "LEFT JOIN file_contents fc ON f.content_id = fc.id " "ORDER BY " + outer_order_by;

                if (has_folder_filter) {
                    auto file_count_result = co_await m_db_client->execSqlCoro(
                        file_count_sql,
                        user_id,
                        search_param,
                        *request.folder_id
                    );
                    if (!file_count_result.empty()) {
                        total += static_cast<int>(file_count_result[0]["cnt"].as<int64_t>());
                    }

                    auto folder_count_result = co_await m_db_client->execSqlCoro(
                        folder_count_sql,
                        user_id,
                        search_param,
                        *request.folder_id
                    );
                    if (!folder_count_result.empty()) {
                        total += static_cast<int>(folder_count_result[0]["cnt"].as<int64_t>());
                    }

                    auto result = co_await m_db_client->execSqlCoro(
                        data_sql,
                        user_id,
                        search_param,
                        *request.folder_id,
                        user_id,
                        search_param,
                        *request.folder_id,
                        request.page_size,
                        offset
                    );

                    for (const auto& row : result) {
                        SearchResultItem item;
                        item.id = row["id"].as<uint64_t>();
                        item.name = row["name"].as<std::string>();
                        item.type = row["type"].as<std::string>();
                        item.size = row["size"].as<uint64_t>();
                        item.mime_type = row["mime_type"].as<std::string>();
                        item.hash = row["hash"].as<std::string>();
                        item.item_count = static_cast<int>(row["item_count"].as<int64_t>());
                        item.path = row["path"].as<std::string>();
                        item.created_at = row["created_at"].as<std::string>();
                        item.updated_at = row["updated_at"].as<std::string>();
                        items.push_back(item);
                    }
                } else {
                    auto file_count_result =
                        co_await m_db_client->execSqlCoro(file_count_sql, user_id, search_param);
                    if (!file_count_result.empty()) {
                        total += static_cast<int>(file_count_result[0]["cnt"].as<int64_t>());
                    }

                    auto folder_count_result =
                        co_await m_db_client->execSqlCoro(folder_count_sql, user_id, search_param);
                    if (!folder_count_result.empty()) {
                        total += static_cast<int>(folder_count_result[0]["cnt"].as<int64_t>());
                    }

                    auto result = co_await m_db_client->execSqlCoro(
                        data_sql,
                        user_id,
                        search_param,
                        user_id,
                        search_param,
                        request.page_size,
                        offset
                    );

                    for (const auto& row : result) {
                        SearchResultItem item;
                        item.id = row["id"].as<uint64_t>();
                        item.name = row["name"].as<std::string>();
                        item.type = row["type"].as<std::string>();
                        item.size = row["size"].as<uint64_t>();
                        item.mime_type = row["mime_type"].as<std::string>();
                        item.hash = row["hash"].as<std::string>();
                        item.item_count = static_cast<int>(row["item_count"].as<int64_t>());
                        item.path = row["path"].as<std::string>();
                        item.created_at = row["created_at"].as<std::string>();
                        item.updated_at = row["updated_at"].as<std::string>();
                        items.push_back(item);
                    }
                }

            } else if (request.type == "file") {
                const std::string count_sql =
                    "SELECT COUNT(*) AS cnt FROM files f " + file_where;
                const std::string data_sql =
                    "SELECT f.id, f.name, f.size, f.mime_type, f.path, f.created_at, f.updated_at, " "       COALESCE(fc.hash_md5, '') AS hash " "FROM (" "  SELECT f.id, f.name " "  FROM files f " + file_where + " " "  ORDER BY " + inner_order_by + " " "  LIMIT $3 OFFSET $4" ") AS page " "JOIN files f ON f.id = page.id " "LEFT JOIN file_contents fc ON f.content_id = fc.id " "ORDER BY " + outer_order_by;

                if (has_folder_filter) {
                    auto count_result = co_await m_db_client->execSqlCoro(
                        count_sql,
                        user_id,
                        search_param,
                        *request.folder_id
                    );

                    if (!count_result.empty()) {
                        total = static_cast<int>(count_result[0]["cnt"].as<int64_t>());
                    }

                    auto result = co_await m_db_client->execSqlCoro(
                        data_sql,
                        user_id,
                        search_param,
                        *request.folder_id,
                        request.page_size,
                        offset
                    );

                    for (const auto& row : result) {
                        SearchResultItem item;
                        item.id = row["id"].as<uint64_t>();
                        item.name = row["name"].as<std::string>();
                        item.type = "file";
                        item.size = row["size"].as<uint64_t>();
                        item.mime_type = row["mime_type"].as<std::string>();
                        item.hash = row["hash"].as<std::string>();
                        item.path = row["path"].as<std::string>();
                        item.created_at = row["created_at"].as<std::string>();
                        item.updated_at = row["updated_at"].as<std::string>();
                        items.push_back(item);
                    }
                } else {
                    auto count_result =
                        co_await m_db_client->execSqlCoro(count_sql, user_id, search_param);

                    if (!count_result.empty()) {
                        total = static_cast<int>(count_result[0]["cnt"].as<int64_t>());
                    }

                    auto result = co_await m_db_client->execSqlCoro(
                        data_sql,
                        user_id,
                        search_param,
                        request.page_size,
                        offset
                    );

                    for (const auto& row : result) {
                        SearchResultItem item;
                        item.id = row["id"].as<uint64_t>();
                        item.name = row["name"].as<std::string>();
                        item.type = "file";
                        item.size = row["size"].as<uint64_t>();
                        item.mime_type = row["mime_type"].as<std::string>();
                        item.hash = row["hash"].as<std::string>();
                        item.path = row["path"].as<std::string>();
                        item.created_at = row["created_at"].as<std::string>();
                        item.updated_at = row["updated_at"].as<std::string>();
                        items.push_back(item);
                    }
                }

            } else if (request.type == "folder") {
                const std::string count_sql =
                    "SELECT COUNT(*) AS cnt FROM folders fo " + folder_where;
                const std::string data_sql =
                    "SELECT fo.id, fo.name, fo.item_count, fo.path, fo.created_at, fo.updated_at " "FROM (" "  SELECT fo.id, fo.name " "  FROM folders fo " + folder_where + " " "  ORDER BY " + inner_order_by + " " "  LIMIT $3 OFFSET $4" ") AS page " "JOIN folders fo ON fo.id = page.id " "ORDER BY " + outer_order_by;

                if (has_folder_filter) {
                    auto count_result = co_await m_db_client->execSqlCoro(
                        count_sql,
                        user_id,
                        search_param,
                        *request.folder_id
                    );

                    if (!count_result.empty()) {
                        total = static_cast<int>(count_result[0]["cnt"].as<int64_t>());
                    }

                    auto result = co_await m_db_client->execSqlCoro(
                        data_sql,
                        user_id,
                        search_param,
                        *request.folder_id,
                        request.page_size,
                        offset
                    );

                    for (const auto& row : result) {
                        SearchResultItem item;
                        item.id = row["id"].as<uint64_t>();
                        item.name = row["name"].as<std::string>();
                        item.type = "folder";
                        item.item_count = static_cast<int>(row["item_count"].as<int64_t>());
                        item.path = row["path"].as<std::string>();
                        item.created_at = row["created_at"].as<std::string>();
                        item.updated_at = row["updated_at"].as<std::string>();
                        items.push_back(item);
                    }
                } else {
                    auto count_result =
                        co_await m_db_client->execSqlCoro(count_sql, user_id, search_param);

                    if (!count_result.empty()) {
                        total = static_cast<int>(count_result[0]["cnt"].as<int64_t>());
                    }

                    auto result = co_await m_db_client->execSqlCoro(
                        data_sql,
                        user_id,
                        search_param,
                        request.page_size,
                        offset
                    );

                    for (const auto& row : result) {
                        SearchResultItem item;
                        item.id = row["id"].as<uint64_t>();
                        item.name = row["name"].as<std::string>();
                        item.type = "folder";
                        item.item_count = static_cast<int>(row["item_count"].as<int64_t>());
                        item.path = row["path"].as<std::string>();
                        item.created_at = row["created_at"].as<std::string>();
                        item.updated_at = row["updated_at"].as<std::string>();
                        items.push_back(item);
                    }
                }
            }

            total_pages = request.page_size > 0 ? (total + request.page_size - 1) / request.page_size : 0;

        } catch (const drogon::orm::DrogonDbException& e) {
            Logger::Warn() << "Failed to search: " << e.base().what();
        }

        SearchResponse response;
        response.items = items;
        response.pagination = { .page = request.page,
                                .page_size = request.page_size,
                                .total = total,
                                .total_pages = total_pages };

        Logger::Debug() << "Search completed: total=" << total << ", page=" << request.page;
        co_return response;
    }

} ///< namespace disk::file
