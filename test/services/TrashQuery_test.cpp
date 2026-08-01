/**
 * @file TrashQuery_test.cpp
 * @brief Trash query boundary contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include "services/TrashQuery.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

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
            using LockedFetchSignature = drogon::Task<std::optional<TrashLifecycleRecord>> (TrashQuery::*)(const drogon::orm::DbClientPtr&, uint64_t, uint64_t) const;
            using UserFetchSignature = drogon::Task<std::vector<TrashLifecycleRecord>> (TrashQuery::*)(uint64_t) const;
            using ExpiredFetchSignature = drogon::Task<std::vector<TrashLifecycleRecord>> (TrashQuery::*)(uint64_t, int) const;

            static_assert(std::is_same_v<decltype(&TrashQuery::FetchListPageForUser), ListSignature>);
            static_assert(std::is_same_v<decltype(&TrashQuery::CountForUser), CountSignature>);
            static_assert(std::is_same_v<decltype(&TrashQuery::PrefetchLifecycleRowsByIds), IdPrefetchSignature>);
            static_assert(std::is_same_v<decltype(&TrashQuery::FetchLifecycleRowForUpdate), LockedFetchSignature>);
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

        TEST(TrashQuerySqlContractTest, FileRestoreLocksAndConsumesTrashAtomically) {
            const auto query_source = ReadSourceFile("src/services/TrashQuery.cpp");
            const auto service_source = ReadSourceFile("src/services/TrashService.cpp");
            const auto restore_begin = service_source.find(
                "auto TrashService::RestoreFile(\n        const TrashLifecycleRecord&"
            );
            const auto restore_end = service_source.find(
                "auto TrashService::RestoreFolder(",
                restore_begin
            );

            ASSERT_NE(restore_begin, std::string::npos);
            ASSERT_NE(restore_end, std::string::npos);
            const auto restore_body = service_source.substr(
                restore_begin,
                restore_end - restore_begin
            );

            EXPECT_TRUE(Contains(query_source, "auto TrashQuery::FetchLifecycleRowForUpdate("));
            EXPECT_TRUE(Contains(query_source, "WHERE id = $1 AND user_id = $2 FOR UPDATE"));

            const auto transaction = restore_body.find("transaction_runner.Run(");
            const auto trash_lock = restore_body.find("FetchLifecycleRowForUpdate(", transaction);
            const auto name_lock = restore_body.find("AcquireNameLock(", trash_lock);
            const auto parent_lock = restore_body.find("FindOwnedFolderForUpdate(", name_lock);
            const auto file_insert = restore_body.find("file_mapper.insert(", parent_lock);
            const auto trash_delete = restore_body.find("DELETE FROM trash", file_insert);
            const auto affected_rows = restore_body.find("affectedRows() != 1", trash_delete);

            ASSERT_NE(transaction, std::string::npos);
            ASSERT_NE(trash_lock, std::string::npos);
            ASSERT_NE(name_lock, std::string::npos);
            ASSERT_NE(parent_lock, std::string::npos);
            ASSERT_NE(file_insert, std::string::npos);
            ASSERT_NE(trash_delete, std::string::npos);
            ASSERT_NE(affected_rows, std::string::npos);
            EXPECT_LT(transaction, trash_lock);
            EXPECT_LT(trash_lock, name_lock);
            EXPECT_LT(name_lock, parent_lock);
            EXPECT_LT(parent_lock, file_insert);
            EXPECT_LT(file_insert, trash_delete);
            EXPECT_LT(trash_delete, affected_rows);
        }

        TEST(TrashQuerySqlContractTest, FolderRestoreRebuildsAndConsumesTrashAtomically) {
            const auto query_source = ReadSourceFile("src/services/TrashQuery.cpp");
            const auto service_source = ReadSourceFile("src/services/TrashService.cpp");
            const auto restore_begin = service_source.find(
                "auto TrashService::RestoreFolder(\n        const TrashLifecycleRecord&"
            );
            const auto restore_end = service_source.find(
                "auto TrashService::PermanentlyDeleteTrashItems(",
                restore_begin
            );

            ASSERT_NE(restore_begin, std::string::npos);
            ASSERT_NE(restore_end, std::string::npos);
            const auto restore_body = service_source.substr(
                restore_begin,
                restore_end - restore_begin
            );

            const auto transaction = restore_body.find("transaction_runner.Run(");
            const auto trash_lock = restore_body.find("FetchLifecycleRowForUpdate(", transaction);
            const auto snapshot = restore_body.find("ParseFolderTreeSnapshot(", trash_lock);
            const auto name_lock = restore_body.find("AcquireNameLock(", snapshot);
            const auto parent_lock = restore_body.find("FindOwnedFolderForUpdate(", name_lock);
            const auto conflict_check = restore_body.find("NameExistsExcluding(", parent_lock);
            const auto root_insert = restore_body.find("folder_mapper.insert(root_folder)", conflict_check);
            const auto file_insert = restore_body.find("file_mapper.insert(file)", root_insert);
            const auto parent_count = restore_body.find("ApplyItemCountDelta(", file_insert);
            const auto trash_delete = restore_body.find("DELETE FROM trash", parent_count);
            const auto affected_rows = restore_body.find("affectedRows() != 1", trash_delete);

            EXPECT_TRUE(Contains(query_source, "WHERE id = $1 AND user_id = $2 FOR UPDATE"));
            ASSERT_NE(transaction, std::string::npos);
            ASSERT_NE(trash_lock, std::string::npos);
            ASSERT_NE(snapshot, std::string::npos);
            ASSERT_NE(name_lock, std::string::npos);
            ASSERT_NE(parent_lock, std::string::npos);
            ASSERT_NE(conflict_check, std::string::npos);
            ASSERT_NE(root_insert, std::string::npos);
            ASSERT_NE(file_insert, std::string::npos);
            ASSERT_NE(parent_count, std::string::npos);
            ASSERT_NE(trash_delete, std::string::npos);
            ASSERT_NE(affected_rows, std::string::npos);
            EXPECT_LT(transaction, trash_lock);
            EXPECT_LT(trash_lock, snapshot);
            EXPECT_LT(snapshot, name_lock);
            EXPECT_LT(name_lock, parent_lock);
            EXPECT_LT(parent_lock, conflict_check);
            EXPECT_LT(conflict_check, root_insert);
            EXPECT_LT(root_insert, file_insert);
            EXPECT_LT(file_insert, parent_count);
            EXPECT_LT(parent_count, trash_delete);
            EXPECT_LT(trash_delete, affected_rows);
        }

        TEST(TrashQuerySqlContractTest, FileTrashTransitionsUpdateParentCountsAtomically) {
            const auto service_source = ReadSourceFile("src/services/TrashService.cpp");
            const auto move_begin = service_source.find("auto TrashService::MoveToTrash(");
            const auto move_end = service_source.find(
                "auto TrashService::CleanupShareLinksForMovedItems(",
                move_begin
            );
            const auto restore_begin = service_source.find(
                "auto TrashService::RestoreFile(\n        const TrashLifecycleRecord&"
            );
            const auto restore_end = service_source.find(
                "auto TrashService::RestoreFolder(",
                restore_begin
            );

            ASSERT_NE(move_begin, std::string::npos);
            ASSERT_NE(move_end, std::string::npos);
            ASSERT_NE(restore_begin, std::string::npos);
            ASSERT_NE(restore_end, std::string::npos);
            const auto move_body = service_source.substr(move_begin, move_end - move_begin);
            const auto restore_body = service_source.substr(
                restore_begin,
                restore_end - restore_begin
            );

            const auto trash_insert = move_body.find(
                "co_await disk::file::utils::InsertTrashRecords("
            );
            const auto parent_deltas = move_body.find(
                "parent_deltas",
                trash_insert
            );
            const auto delete_count_update = move_body.find(
                "ApplyItemCountDelta(",
                parent_deltas
            );
            const auto active_delete = move_body.find("DeleteFilesByIds(", delete_count_update);
            ASSERT_NE(trash_insert, std::string::npos);
            ASSERT_NE(parent_deltas, std::string::npos);
            ASSERT_NE(delete_count_update, std::string::npos);
            ASSERT_NE(active_delete, std::string::npos);
            EXPECT_LT(trash_insert, parent_deltas);
            EXPECT_LT(parent_deltas, delete_count_update);
            EXPECT_LT(delete_count_update, active_delete);

            const auto active_insert = restore_body.find("file_mapper.insert(");
            const auto restore_count_update = restore_body.find(
                "ApplyItemCountDelta(",
                active_insert
            );
            const auto trash_delete = restore_body.find(
                "DELETE FROM trash",
                restore_count_update
            );
            ASSERT_NE(active_insert, std::string::npos);
            ASSERT_NE(restore_count_update, std::string::npos);
            ASSERT_NE(trash_delete, std::string::npos);
            EXPECT_LT(active_insert, restore_count_update);
            EXPECT_LT(restore_count_update, trash_delete);
        }

        TEST(TrashQuerySqlContractTest, ExpiredFetchKeepsCursorAndLifecycleInService) {
            const auto query_source = ReadSourceFile("src/services/TrashQuery.cpp");
            const auto service_source = ReadSourceFile("src/services/TrashService.cpp");

            EXPECT_TRUE(Contains(query_source, "auto TrashQuery::FetchExpiredLifecycleBatchAfterId("));
            EXPECT_TRUE(Contains(query_source, "WHERE expires_at < NOW() AND id > $1 "));
            EXPECT_TRUE(Contains(query_source, "ORDER BY id ASC "));
            EXPECT_TRUE(Contains(query_source, "LIMIT $2"));

            EXPECT_TRUE(Contains(service_source, "m_trash_query.FetchExpiredLifecycleBatchAfterId("));
            EXPECT_TRUE(Contains(service_source, "auto TrashService::CleanupExpiredTrashPage("));
            EXPECT_TRUE(Contains(service_source, "page.next_after_id = trash_items.back().id;"));
            EXPECT_TRUE(Contains(service_source, "Expired trash page contains an invalid content reference"));
            EXPECT_TRUE(Contains(service_source, "PermanentlyDeleteTrashItems("));
            EXPECT_TRUE(Contains(service_source, "false,\n                    log_context"));
            EXPECT_TRUE(Contains(service_source, "DecrementRefCountsAndEnqueueGc("));
            EXPECT_FALSE(Contains(service_source, "CleanupVerifiedZeroRefBlobs("));
            EXPECT_FALSE(Contains(service_source, "DeleteBlob("));
            EXPECT_FALSE(Contains(query_source, "PermanentlyDeleteTrashItems"));
            EXPECT_FALSE(Contains(query_source, "CleanupVerifiedZeroRefBlobs"));
            EXPECT_FALSE(Contains(query_source, "AdjustUsedStorage"));
            EXPECT_FALSE(Contains(query_source, "DecrementRefCounts"));
        }

    } // namespace
} // namespace disk::trash
