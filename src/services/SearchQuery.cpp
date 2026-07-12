/**
 * @file SearchQuery.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件搜索查询对象实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "SearchQuery.hpp"

#include <string>

#include "FileServiceUtils.hpp"

namespace disk::file {

    SearchQuery::SearchQuery(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
    }

    auto SearchQuery::Execute(SearchQueryParams request, uint64_t user_id) -> drogon::Task<SearchResponse> {
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
        auto offset = request.Offset();

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
                "SELECT page.id, page.name, page.type, "
                "       COALESCE(f.size, 0) AS size, "
                "       COALESCE(f.mime_type, '') AS mime_type, "
                "       COALESCE(fc.hash_md5, '') AS hash, "
                "       COALESCE(fo.item_count, 0) AS item_count, "
                "       COALESCE(f.path, fo.path) AS path, "
                "       COALESCE(f.created_at, fo.created_at) AS created_at, "
                "       COALESCE(f.updated_at, fo.updated_at) AS updated_at "
                "FROM ("
                "  SELECT combined.id, combined.name, combined.type "
                "  FROM ("
                "    SELECT f.id, f.name, 'file' AS type "
                "    FROM files f " + file_where + " "
                "    UNION ALL "
                "    SELECT fo.id, fo.name, 'folder' AS type "
                "    FROM folders fo " + folder_where + " "
                "  ) AS combined "
                "  ORDER BY " + inner_order_by + " "
                "  LIMIT " + std::to_string(request.page_size) + " OFFSET " + std::to_string(offset) +
                ") AS page "
                "LEFT JOIN files f ON page.type = 'file' AND f.id = page.id "
                "LEFT JOIN folders fo ON page.type = 'folder' AND fo.id = page.id "
                "LEFT JOIN file_contents fc ON f.content_id = fc.id "
                "ORDER BY " + outer_order_by;

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
                    *request.folder_id
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
                    search_param
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
                "SELECT f.id, f.name, f.size, f.mime_type, f.path, f.created_at, f.updated_at, "
                "       COALESCE(fc.hash_md5, '') AS hash "
                "FROM ("
                "  SELECT f.id, f.name "
                "  FROM files f " + file_where + " "
                "  ORDER BY " + inner_order_by + " "
                "  LIMIT " + std::to_string(request.page_size) + " OFFSET " + std::to_string(offset) +
                ") AS page "
                "JOIN files f ON f.id = page.id "
                "LEFT JOIN file_contents fc ON f.content_id = fc.id "
                "ORDER BY " + outer_order_by;

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
                    *request.folder_id
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
                    search_param
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
                "SELECT fo.id, fo.name, fo.item_count, fo.path, fo.created_at, fo.updated_at "
                "FROM ("
                "  SELECT fo.id, fo.name "
                "  FROM folders fo " + folder_where + " "
                "  ORDER BY " + inner_order_by + " "
                "  LIMIT " + std::to_string(request.page_size) + " OFFSET " + std::to_string(offset) +
                ") AS page "
                "JOIN folders fo ON fo.id = page.id "
                "ORDER BY " + outer_order_by;

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
                    *request.folder_id
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
                    search_param
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

        SearchResponse response;
        response.items = std::move(items);
        response.pagination = { .page = request.page,
                                .page_size = request.page_size,
                                .total = total,
                                .total_pages = total_pages };
        co_return response;
    }

} ///< namespace disk::file
