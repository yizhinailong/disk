/**
 * @file FileListQuery.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件列表查询对象实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "FileListQuery.hpp"

#include <string>

#include "FileServiceUtils.hpp"

namespace disk::file {

    FileListQuery::FileListQuery(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
    }

    auto FileListQuery::Execute(FileListQueryParams request, uint64_t user_id)
        -> drogon::Task<FileListResponse> {
        const auto offset = request.Offset();

        std::vector<FileListItem> items;
        int total = 0;

        if (request.type == "all") {
            auto [queried_items, queried_total] = co_await queryAll(request, user_id, offset);
            items = std::move(queried_items);
            total = queried_total;
        } else if (request.type == "file") {
            auto [queried_items, queried_total] = co_await queryFiles(request, user_id, offset);
            items = std::move(queried_items);
            total = queried_total;
        } else if (request.type == "folder") {
            auto [queried_items, queried_total] = co_await queryFolders(request, user_id, offset);
            items = std::move(queried_items);
            total = queried_total;
        }

        FileListResponse response;
        response.items = std::move(items);
        response.pagination = { .page = request.page,
                                .page_size = request.page_size,
                                .total = total,
                                .total_pages = request.page_size > 0 ?
                                                   (total + request.page_size - 1) / request.page_size :
                                                   0 };
        co_return response;
    }

    auto FileListQuery::queryAll(FileListQueryParams const& request, uint64_t user_id, int64_t offset)
        -> drogon::Task<std::pair<std::vector<FileListItem>, int>> {
        int total = 0;
        std::vector<FileListItem> items;

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

        const auto order_column = utils::ResolveListSortColumn(request.sort_by, false);
        const std::string order_dir = (request.sort_order == "desc") ? "DESC" : "ASC";
        const std::string inner_order_by =
            utils::BuildDeterministicOrderByClause(order_column, order_dir, true);
        const std::string outer_order_by = utils::BuildDeterministicOrderByClause(
            order_column,
            order_dir,
            true,
            "page."
        );

        /// 先在窄行结果集上完成分页，再回表补齐详情，避免在宽行 UNION 结果上提前排序。
        const std::string data_sql =
            "SELECT page.id, page.name, page.type, page.size, "
            "       COALESCE(f.mime_type, '') AS mime_type, "
            "       COALESCE(fc.hash_md5, '') AS hash, "
            "       COALESCE(fo.item_count, 0) AS item_count, "
            "       page.created_at, page.updated_at "
            "FROM ("
            "  SELECT combined.id, combined.name, combined.type, combined.size, combined.created_at, combined.updated_at "
            "  FROM ("
            "    SELECT f.id, f.name, 'file' AS type, f.size, f.created_at, f.updated_at "
            "    FROM files f "
            "    WHERE f.folder_id = $1 AND f.user_id = $2 "
            "    UNION ALL "
            "    SELECT fo.id, fo.name, 'folder' AS type, 0 AS size, fo.created_at, fo.updated_at "
            "    FROM folders fo "
            "    WHERE fo.parent_id = $3 AND fo.user_id = $4 "
            "  ) AS combined "
            "  ORDER BY " + inner_order_by + " "
            "  LIMIT " + std::to_string(request.page_size) + " OFFSET " + std::to_string(offset) +
            ") AS page "
            "LEFT JOIN files f ON page.type = 'file' AND page.id = f.id "
            "LEFT JOIN folders fo ON page.type = 'folder' AND page.id = fo.id "
            "LEFT JOIN file_contents fc ON f.content_id = fc.id "
            "ORDER BY " + outer_order_by;

        auto paginated_result = co_await m_db_client->execSqlCoro(
            data_sql,
            request.parent_id,
            user_id,
            request.parent_id,
            user_id
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

        co_return std::pair<std::vector<FileListItem>, int>{ std::move(items), total };
    }

    auto FileListQuery::queryFiles(FileListQueryParams const& request, uint64_t user_id, int64_t offset)
        -> drogon::Task<std::pair<std::vector<FileListItem>, int>> {
        int total = 0;
        std::vector<FileListItem> items;

        auto count_result = co_await m_db_client->execSqlCoro(
            "SELECT COUNT(*) AS cnt FROM files WHERE folder_id = $1 AND user_id = $2",
            request.parent_id,
            user_id
        );

        if (!count_result.empty()) {
            total = static_cast<int>(count_result[0]["cnt"].as<int64_t>());
        }

        const auto order_column = utils::ResolveListSortColumn(request.sort_by, false);
        const std::string order_dir = (request.sort_order == "desc") ? "DESC" : "ASC";
        const std::string inner_order_by =
            utils::BuildDeterministicOrderByClause(order_column, order_dir, false);
        const std::string outer_order_by = utils::BuildDeterministicOrderByClause(
            order_column,
            order_dir,
            false,
            "page."
        );

        const std::string data_sql =
            "SELECT f.id, f.name, f.size, f.mime_type, "
            "       COALESCE(fc.hash_md5, '') AS hash, f.created_at, f.updated_at "
            "FROM ("
            "  SELECT f.id, f.name, f.size, f.created_at, f.updated_at "
            "  FROM files f "
            "  WHERE f.folder_id = $1 AND f.user_id = $2 "
            "  ORDER BY " + inner_order_by + " "
            "  LIMIT " + std::to_string(request.page_size) + " OFFSET " + std::to_string(offset) +
            ") AS page "
            "JOIN files f ON f.id = page.id "
            "LEFT JOIN file_contents fc ON f.content_id = fc.id "
            "ORDER BY " + outer_order_by;

        auto paginated_result = co_await m_db_client->execSqlCoro(
            data_sql,
            request.parent_id,
            user_id
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

        co_return std::pair<std::vector<FileListItem>, int>{ std::move(items), total };
    }

    auto FileListQuery::queryFolders(FileListQueryParams const& request, uint64_t user_id, int64_t offset)
        -> drogon::Task<std::pair<std::vector<FileListItem>, int>> {
        int total = 0;
        std::vector<FileListItem> items;

        auto count_result = co_await m_db_client->execSqlCoro(
            "SELECT COUNT(*) AS cnt FROM folders WHERE parent_id = $1 AND user_id = $2",
            request.parent_id,
            user_id
        );

        if (!count_result.empty()) {
            total = static_cast<int>(count_result[0]["cnt"].as<int64_t>());
        }

        const auto order_column = utils::ResolveListSortColumn(request.sort_by, true);
        const std::string order_dir = (request.sort_order == "desc") ? "DESC" : "ASC";
        const std::string inner_order_by =
            utils::BuildDeterministicOrderByClause(order_column, order_dir, false);
        const std::string outer_order_by = utils::BuildDeterministicOrderByClause(
            order_column,
            order_dir,
            false,
            "page."
        );

        const std::string data_sql =
            "SELECT page.id, page.name, page.item_count, page.created_at, page.updated_at "
            "FROM ("
            "  SELECT fo.id, fo.name, fo.item_count, fo.created_at, fo.updated_at, 0 AS sort_size "
            "  FROM folders fo "
            "  WHERE fo.parent_id = $1 AND fo.user_id = $2 "
            "  ORDER BY " + inner_order_by + " "
            "  LIMIT " + std::to_string(request.page_size) + " OFFSET " + std::to_string(offset) +
            ") AS page "
            "ORDER BY " + outer_order_by;

        auto paginated_result = co_await m_db_client->execSqlCoro(
            data_sql,
            request.parent_id,
            user_id
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

        co_return std::pair<std::vector<FileListItem>, int>{ std::move(items), total };
    }

} ///< namespace disk::file
