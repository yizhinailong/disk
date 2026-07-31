/**
 * @file TrashQuery.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 回收站查询边界实现
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "TrashQuery.hpp"

#include <utility>

#include "utils/BatchUtils.hpp"

namespace disk::trash {

    using disk::utils::BatchUtils;
    using disk::utils::DEFAULT_BATCH_CHUNK_SIZE;

    namespace {

        [[nodiscard]] auto MapLifecycleRecord(const drogon::orm::Row& row) -> TrashLifecycleRecord {
            TrashLifecycleRecord item;
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
            return item;
        }

        [[nodiscard]] auto MapLifecycleRecords(const drogon::orm::Result& result)
            -> std::vector<TrashLifecycleRecord> {
            std::vector<TrashLifecycleRecord> records;
            records.reserve(result.size());
            for (const auto& row : result) {
                records.push_back(MapLifecycleRecord(row));
            }
            return records;
        }

    } ///< namespace

    TrashQuery::TrashQuery(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
    }

    auto TrashQuery::FetchListPageForUser(uint64_t user_id, int limit, int offset) const
        -> drogon::Task<std::vector<TrashListRecord>> {
        auto result = co_await m_db_client->execSqlCoro(
            "SELECT id, user_id, item_type, item_id, item_name, item_size, "
            "original_folder_id, original_path, item_data, deleted_at, expires_at "
            "FROM trash "
            "WHERE user_id = $1 "
            "ORDER BY deleted_at DESC "
            "LIMIT $2 OFFSET $3",
            user_id,
            static_cast<int64_t>(limit),
            static_cast<int64_t>(offset)
        );

        std::vector<TrashListRecord> records;
        records.reserve(result.size());
        for (const auto& row : result) {
            records.push_back(TrashListRecord{
                .id = row["id"].as<uint64_t>(),
                .item_type = row["item_type"].as<std::string>(),
                .item_id = row["item_id"].as<uint64_t>(),
                .item_name = row["item_name"].as<std::string>(),
                .item_size = row["item_size"].as<uint64_t>(),
                .original_path = row["original_path"].as<std::string>(),
                .deleted_at = row["deleted_at"].as<std::string>(),
                .expires_at = row["expires_at"].as<std::string>(),
            });
        }

        co_return records;
    }

    auto TrashQuery::CountForUser(uint64_t user_id) const -> drogon::Task<int> {
        auto result = co_await m_db_client->execSqlCoro(
            "SELECT COUNT(*) AS count FROM trash WHERE user_id = $1",
            user_id
        );
        co_return result.empty() ? 0 : result[0]["count"].as<int>();
    }

    auto TrashQuery::PrefetchLifecycleRowsByIds(const std::vector<uint64_t>& trash_ids) const
        -> drogon::Task<std::vector<TrashLifecycleRecord>> {
        if (trash_ids.empty()) {
            co_return std::vector<TrashLifecycleRecord>{};
        }

        std::vector<TrashLifecycleRecord> records;
        auto chunks = BatchUtils::Chunk(trash_ids, DEFAULT_BATCH_CHUNK_SIZE);
        for (const auto& chunk : chunks) {
            if (chunk.empty()) {
                continue;
            }

            auto result = co_await m_db_client->execSqlCoro(
                "SELECT id, user_id, item_type, item_id, item_name, item_size, "
                "original_folder_id, original_path, item_data, content_id "
                "FROM trash WHERE id IN (" + BatchUtils::BuildSafeNumericInClause(chunk) + ")"
            );
            auto chunk_records = MapLifecycleRecords(result);
            records.insert(
                records.end(),
                std::make_move_iterator(chunk_records.begin()),
                std::make_move_iterator(chunk_records.end())
            );
        }

        co_return records;
    }

    auto TrashQuery::FetchLifecycleRowForUpdate(
        const drogon::orm::DbClientPtr& client,
        uint64_t trash_id,
        uint64_t user_id
    ) const -> drogon::Task<std::optional<TrashLifecycleRecord>> {
        auto result = co_await client->execSqlCoro(
            "SELECT id, user_id, item_type, item_id, item_name, item_size, "
            "original_folder_id, original_path, item_data, content_id "
            "FROM trash WHERE id = $1 AND user_id = $2 FOR UPDATE",
            trash_id,
            user_id
        );
        if (result.empty()) {
            co_return std::nullopt;
        }
        co_return MapLifecycleRecord(result[0]);
    }

    auto TrashQuery::FetchLifecycleRowsForUser(uint64_t user_id) const
        -> drogon::Task<std::vector<TrashLifecycleRecord>> {
        auto result = co_await m_db_client->execSqlCoro(
            "SELECT id, user_id, item_type, item_id, item_name, item_size, "
            "original_folder_id, original_path, item_data, content_id "
            "FROM trash WHERE user_id = $1",
            user_id
        );

        co_return MapLifecycleRecords(result);
    }

    auto TrashQuery::FetchExpiredLifecycleBatchAfterId(uint64_t last_seen_id, int limit) const
        -> drogon::Task<std::vector<TrashLifecycleRecord>> {
        auto result = co_await m_db_client->execSqlCoro(
            "SELECT id, user_id, item_type, item_id, item_name, item_size, "
            "original_folder_id, original_path, item_data, content_id "
            "FROM trash "
            "WHERE expires_at < NOW() AND id > $1 "
            "ORDER BY id ASC "
            "LIMIT $2",
            last_seen_id,
            static_cast<int64_t>(limit)
        );

        co_return MapLifecycleRecords(result);
    }

} ///< namespace disk::trash
