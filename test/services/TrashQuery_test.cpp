/**
 * @file TrashQuery_test.cpp
 * @brief Trash query boundary contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "services/TrashQuery.hpp"

namespace disk::trash {
    namespace {

        auto RepositoryRoot() -> std::filesystem::path {
            return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
        }

        auto ReadSourceFile(const std::filesystem::path& relative_path) -> std::string {
            std::ifstream input(RepositoryRoot() / relative_path);
            std::ostringstream buffer;
            buffer << input.rdbuf();
            return buffer.str();
        }

        auto Contains(const std::string& source, const std::string& expected) -> bool {
            return source.find(expected) != std::string::npos;
        }

        TEST(TrashQueryRecordContractTest, LifecycleRecordCarriesOnlyFetchedFacts) {
            TrashLifecycleRecord record;
            record.id = 1;
            record.user_id = 2;
            record.item_type = "file";
            record.item_id = 3;
            record.item_name = "example.txt";
            record.item_size = 42;
            record.original_folder_id = 4;
            record.original_path = "/example.txt";
            record.item_data = R"({"content_id":5})";
            record.content_id = 5;

            EXPECT_EQ(record.id, 1U);
            EXPECT_EQ(record.user_id, 2U);
            EXPECT_EQ(record.item_type, "file");
            EXPECT_EQ(record.item_id, 3U);
            EXPECT_EQ(record.item_name, "example.txt");
            EXPECT_EQ(record.item_size, 42U);
            EXPECT_EQ(record.original_folder_id, 4U);
            EXPECT_EQ(record.original_path, "/example.txt");
            ASSERT_TRUE(record.content_id.has_value());
            EXPECT_EQ(record.content_id.value(), 5U);
        }

        TEST(TrashQuerySignatureContractTest, ExposesExplicitReadBoundaryMethods) {
            using ListSignature = drogon::Task<std::vector<TrashListRecord>> (TrashQuery::*)(uint64_t, int, int) const;
            using CountSignature = drogon::Task<int> (TrashQuery::*)(uint64_t) const;
            using IdPrefetchSignature = drogon::Task<std::vector<TrashLifecycleRecord>> (TrashQuery::*)(const std::vector<uint64_t>&) const;
            using UserFetchSignature = drogon::Task<std::vector<TrashLifecycleRecord>> (TrashQuery::*)(uint64_t) const;
            using ExpiredFetchSignature = drogon::Task<std::vector<TrashLifecycleRecord>> (TrashQuery::*)(uint64_t, int) const;

            static_assert(std::is_same_v<decltype(&TrashQuery::FetchListPageForUser), ListSignature>);
            static_assert(std::is_same_v<decltype(&TrashQuery::CountForUser), CountSignature>);
            static_assert(std::is_same_v<decltype(&TrashQuery::PrefetchLifecycleRowsByIds), IdPrefetchSignature>);
            static_assert(std::is_same_v<decltype(&TrashQuery::FetchLifecycleRowsForUser), UserFetchSignature>);
            static_assert(std::is_same_v<decltype(&TrashQuery::FetchExpiredLifecycleBatchAfterId), ExpiredFetchSignature>);
        }

        TEST(TrashQuerySqlContractTest, ListAndCountStayUserScopedAndPaginated) {
            const auto query_source = ReadSourceFile("src/services/TrashQuery.cpp");

            EXPECT_TRUE(Contains(query_source, "auto TrashQuery::FetchListPageForUser("));
            EXPECT_TRUE(Contains(query_source, "WHERE user_id = $1 "));
            EXPECT_TRUE(Contains(query_source, "ORDER BY deleted_at DESC "));
            EXPECT_TRUE(Contains(query_source, "LIMIT $2 OFFSET $3"));

            EXPECT_TRUE(Contains(query_source, "auto TrashQuery::CountForUser("));
            EXPECT_TRUE(Contains(query_source, "SELECT COUNT(*) AS count FROM trash WHERE user_id = $1"));
        }

        TEST(TrashQuerySqlContractTest, RestoreDeletePrefetchDoesNotOwnAuthorizationDecision) {
            const auto query_source = ReadSourceFile("src/services/TrashQuery.cpp");
            const auto service_source = ReadSourceFile("src/services/TrashService.cpp");

            EXPECT_TRUE(Contains(query_source, "auto TrashQuery::PrefetchLifecycleRowsByIds("));
            EXPECT_TRUE(Contains(query_source, "if (trash_ids.empty())"));
            EXPECT_TRUE(Contains(query_source, "BatchUtils::Chunk(trash_ids, DEFAULT_BATCH_CHUNK_SIZE)"));
            EXPECT_TRUE(Contains(query_source, "FROM trash WHERE id IN ("));
            EXPECT_FALSE(Contains(query_source, "FROM trash WHERE user_id = $1 AND id IN"));

            EXPECT_TRUE(Contains(service_source, "if (trash_item.user_id != user_id)"));
            EXPECT_TRUE(Contains(service_source, "result.message = \"Trash item not found\";"));
            EXPECT_TRUE(Contains(service_source, "m_trash_query.PrefetchLifecycleRowsByIds(trash_ids)"));
        }

        TEST(TrashQuerySqlContractTest, ExpiredFetchKeepsCursorAndLifecycleInService) {
            const auto query_source = ReadSourceFile("src/services/TrashQuery.cpp");
            const auto service_source = ReadSourceFile("src/services/TrashService.cpp");

            EXPECT_TRUE(Contains(query_source, "auto TrashQuery::FetchExpiredLifecycleBatchAfterId("));
            EXPECT_TRUE(Contains(query_source, "WHERE expires_at < NOW() AND id > $1 "));
            EXPECT_TRUE(Contains(query_source, "ORDER BY id ASC "));
            EXPECT_TRUE(Contains(query_source, "LIMIT $2"));

            EXPECT_TRUE(Contains(service_source, "m_trash_query.FetchExpiredLifecycleBatchAfterId("));
            EXPECT_TRUE(Contains(service_source, "last_seen_id = batch_max_id;"));
            EXPECT_TRUE(Contains(service_source, "PermanentlyDeleteTrashItems(chunk, false)"));
            EXPECT_TRUE(Contains(service_source, "CleanupVerifiedZeroRefBlobs("));
            EXPECT_FALSE(Contains(query_source, "PermanentlyDeleteTrashItems"));
            EXPECT_FALSE(Contains(query_source, "CleanupVerifiedZeroRefBlobs"));
            EXPECT_FALSE(Contains(query_source, "AdjustUsedStorage"));
            EXPECT_FALSE(Contains(query_source, "DecrementRefCounts"));
        }

    } ///< namespace
} ///< namespace disk::trash
