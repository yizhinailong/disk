/**
 * @file FileMutationServiceMove_test.cpp
 * @brief FileMutationService move transaction boundary contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace disk::file {
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

        auto CountOccurrences(const std::string& source, std::string_view expected) -> size_t {
            size_t count = 0;
            size_t position = 0;
            while ((position = source.find(expected, position)) != std::string::npos) {
                ++count;
                position += expected.size();
            }
            return count;
        }

        auto ExtractRange(
            const std::string& source,
            std::string_view begin_marker,
            std::string_view end_marker
        ) -> std::string {
            const auto begin = source.find(begin_marker);
            const auto end = source.find(end_marker, begin);
            if (begin == std::string::npos || end == std::string::npos) {
                return {};
            }
            return source.substr(begin, end - begin);
        }

        auto ExtractMoveBody(const std::string& source) -> std::string {
            return ExtractRange(
                source,
                "auto FileMutationService::Move(",
                "    /// ==================== Copy ===================="
            );
        }

        auto ExtractRenameBody(const std::string& source) -> std::string {
            return ExtractRange(
                source,
                "auto FileMutationService::Rename(",
                "    /// ==================== Move ===================="
            );
        }

        auto ExtractCopyBody(const std::string& source) -> std::string {
            return ExtractRange(
                source,
                "auto FileMutationService::Copy(",
                "    /// ==================== Delete ===================="
            );
        }

        TEST(FileMutationServiceRenameContractTest, RenameUsesOneTransactionAndStableErrors) {
            const auto source = ReadSourceFile("src/services/FileMutationService.cpp");
            const auto rename_body = ExtractRenameBody(source);

            ASSERT_FALSE(rename_body.empty());
            EXPECT_TRUE(Contains(rename_body, "TransactionRunner rename_transaction_runner("));
            EXPECT_TRUE(Contains(rename_body, "rename_transaction_runner.Run("));
            EXPECT_TRUE(Contains(rename_body, "ErrorInfo(ErrorCode::FileAlreadyExists)"));
            EXPECT_TRUE(Contains(rename_body, "ErrorInfo(ErrorCode::FileNotFound)"));
            EXPECT_TRUE(Contains(rename_body, "if (!transaction_result)"));
            EXPECT_FALSE(Contains(rename_body, "IsFilenameExists("));
            EXPECT_FALSE(Contains(rename_body, "catch (const drogon::orm::DrogonDbException"));

            const auto transaction = rename_body.find("rename_transaction_runner.Run(");
            const auto result_check = rename_body.find("if (!transaction_result)", transaction);
            const auto cache_invalidation = rename_body.find("FileListCache::Invalidate", result_check);
            ASSERT_NE(transaction, std::string::npos);
            ASSERT_NE(result_check, std::string::npos);
            ASSERT_NE(cache_invalidation, std::string::npos);
            EXPECT_LT(transaction, result_check);
            EXPECT_LT(result_check, cache_invalidation);
        }

        TEST(FileMutationServiceMoveContractTest, MoveUsesTransactionRunnerBoundary) {
            const auto source = ReadSourceFile("src/services/FileMutationService.cpp");
            const auto utils_header = ReadSourceFile("src/services/FileServiceUtils.hpp");
            const auto move_body = ExtractMoveBody(source);

            ASSERT_FALSE(move_body.empty());
            ASSERT_FALSE(utils_header.empty());
            EXPECT_TRUE(Contains(source, "#include \"TransactionRunner.hpp\""));
            EXPECT_TRUE(Contains(move_body, "TransactionRunner transaction_runner("));
            EXPECT_TRUE(Contains(move_body, "transaction_runner.Run("));
            EXPECT_TRUE(Contains(move_body, "ErrorInfo(ErrorCode::InternalError, \"Failed to move items\")"));

            EXPECT_FALSE(Contains(move_body, "newTransactionCoro"));
            EXPECT_FALSE(Contains(move_body, "txn->rollback"));
            EXPECT_FALSE(Contains(utils_header, "struct ServiceValidationException"));
        }

        TEST(FileMutationServiceMoveContractTest, MoveLocksRowsAndNamesBeforeConflictChecks) {
            const auto source = ReadSourceFile("src/services/FileMutationService.cpp");
            const auto move_body = ExtractMoveBody(source);

            ASSERT_FALSE(move_body.empty());
            const auto transaction = move_body.find("transaction_runner.Run(");
            const auto row_locks = move_body.find("FetchOwnedFilesByIdsForUpdate(", transaction);
            const auto sort_names = move_body.find("std::sort(candidate_names.begin(), candidate_names.end())", row_locks);
            const auto name_locks = move_body.find("m_file_repository.AcquireNameLock(", sort_names);
            const auto conflict_check = move_body.find("utils::QueryOccupiedNames(", name_locks);
            const auto location_update = move_body.find("m_file_repository.UpdateFileLocation(", conflict_check);
            ASSERT_NE(transaction, std::string::npos);
            ASSERT_NE(row_locks, std::string::npos);
            ASSERT_NE(sort_names, std::string::npos);
            ASSERT_NE(name_locks, std::string::npos);
            ASSERT_NE(conflict_check, std::string::npos);
            ASSERT_NE(location_update, std::string::npos);
            EXPECT_LT(transaction, row_locks);
            EXPECT_LT(row_locks, sort_names);
            EXPECT_LT(sort_names, name_locks);
            EXPECT_LT(name_locks, conflict_check);
            EXPECT_LT(conflict_check, location_update);
            EXPECT_TRUE(Contains(move_body, "if (!updated)"));
            EXPECT_TRUE(Contains(move_body, "if (!count_updated)"));
        }

        TEST(FileMutationServiceMoveContractTest, MoveLocksFolderNamesBeforeFolderConflictChecks) {
            const auto source = ReadSourceFile("src/services/FileMutationService.cpp");
            const auto move_body = ExtractMoveBody(source);

            ASSERT_FALSE(move_body.empty());
            const auto folder_plans = move_body.find("FetchBatchFolderDeletePlans(");
            const auto sort_names = move_body.find(
                "std::sort(folder_candidate_names.begin(), folder_candidate_names.end())",
                folder_plans
            );
            const auto name_locks = move_body.find(
                "m_folder_repository.AcquireNameLock(",
                sort_names
            );
            const auto conflict_check = move_body.find(
                "utils::QueryOccupiedFolderNames(",
                name_locks
            );
            const auto location_update = move_body.find(
                "m_folder_repository.UpdateFolderLocationForMove(",
                conflict_check
            );
            ASSERT_NE(folder_plans, std::string::npos);
            ASSERT_NE(sort_names, std::string::npos);
            ASSERT_NE(name_locks, std::string::npos);
            ASSERT_NE(conflict_check, std::string::npos);
            ASSERT_NE(location_update, std::string::npos);
            EXPECT_LT(folder_plans, sort_names);
            EXPECT_LT(sort_names, name_locks);
            EXPECT_LT(name_locks, conflict_check);
            EXPECT_LT(conflict_check, location_update);
        }

        TEST(FileMutationServiceMoveContractTest, MoveLocksFolderRootsBeforeLoadingPlans) {
            const auto source = ReadSourceFile("src/services/FileMutationService.cpp");
            const auto move_body = ExtractMoveBody(source);

            ASSERT_FALSE(move_body.empty());
            const auto transaction = move_body.find("transaction_runner.Run(");
            const auto root_loop = move_body.find(
                "for (const auto folder_id : folder_ids)",
                transaction
            );
            const auto root_lock = move_body.find(
                "m_folder_repository.FindOwnedFolderForUpdate(",
                root_loop
            );
            const auto folder_plans = move_body.find(
                "FetchBatchFolderDeletePlans(",
                root_lock
            );
            ASSERT_NE(transaction, std::string::npos);
            ASSERT_NE(root_loop, std::string::npos);
            ASSERT_NE(root_lock, std::string::npos);
            ASSERT_NE(folder_plans, std::string::npos);
            EXPECT_LT(transaction, root_loop);
            EXPECT_LT(root_loop, root_lock);
            EXPECT_LT(root_lock, folder_plans);
        }

        TEST(FileMutationServiceMoveContractTest, MoveChecksEveryFolderSubtreeWriteResult) {
            const auto source = ReadSourceFile("src/services/FileMutationService.cpp");
            const auto move_body = ExtractMoveBody(source);

            ASSERT_FALSE(move_body.empty());
            const auto root_update = move_body.find("m_folder_repository.UpdateFolderLocationForMove(");
            const auto descendant_update = move_body.find("m_folder_repository.UpdateFolderPathForMove(", root_update);
            const auto folder_check = move_body.find("if (!folder_updated)", descendant_update);
            const auto file_update = move_body.find("m_file_repository.UpdateFilePath(", folder_check);
            const auto file_check = move_body.find("if (!file_updated)", file_update);
            const auto source_count = move_body.find("auto source_count_updated", file_check);
            const auto source_count_check = move_body.find("if (!source_count_updated)", source_count);
            const auto target_count = move_body.find("auto target_count_updated", source_count_check);
            const auto target_count_check = move_body.find("if (!target_count_updated)", target_count);
            ASSERT_NE(root_update, std::string::npos);
            ASSERT_NE(descendant_update, std::string::npos);
            ASSERT_NE(folder_check, std::string::npos);
            ASSERT_NE(file_update, std::string::npos);
            ASSERT_NE(file_check, std::string::npos);
            ASSERT_NE(source_count, std::string::npos);
            ASSERT_NE(source_count_check, std::string::npos);
            ASSERT_NE(target_count, std::string::npos);
            ASSERT_NE(target_count_check, std::string::npos);
            EXPECT_LT(root_update, descendant_update);
            EXPECT_LT(descendant_update, folder_check);
            EXPECT_LT(folder_check, file_update);
            EXPECT_LT(file_update, file_check);
            EXPECT_LT(file_check, source_count);
            EXPECT_LT(source_count, source_count_check);
            EXPECT_LT(source_count_check, target_count);
            EXPECT_LT(target_count, target_count_check);
        }

        TEST(FileMutationServiceMoveContractTest, MovePreservesValidationAndInvalidatesSharedCacheGeneration) {
            const auto source = ReadSourceFile("src/services/FileMutationService.cpp");
            const auto move_body = ExtractMoveBody(source);

            ASSERT_FALSE(move_body.empty());
            EXPECT_TRUE(Contains(move_body, "Cannot move a folder into itself or its descendant"));
            EXPECT_TRUE(Contains(move_body, "co_return std::unexpected(ErrorInfo("));
            EXPECT_TRUE(Contains(
                move_body,
                "co_await FileListCache::Invalidate(m_redis_service, user_id, log_context);"
            ));
            EXPECT_FALSE(Contains(move_body, "BuildFileListCachePrefix"));

            const auto result_check = move_body.find("if (!transaction_result)");
            const auto cache_invalidation = move_body.find("FileListCache::Invalidate");
            ASSERT_NE(result_check, std::string::npos);
            ASSERT_NE(cache_invalidation, std::string::npos);
            EXPECT_LT(result_check, cache_invalidation);
        }

        TEST(FileMutationServiceCopyContractTest, CopyLocksNamesAndRechecksConflictsInsideBatchTransaction) {
            const auto source = ReadSourceFile("src/services/FileMutationService.cpp");
            const auto copy_body = ExtractCopyBody(source);

            ASSERT_FALSE(copy_body.empty());
            const auto transaction = copy_body.find("auto tx_result = co_await transaction_runner.Run(");
            const auto sort_names = copy_body.find("std::sort(transaction_names.begin(), transaction_names.end())", transaction);
            const auto name_locks = copy_body.find("m_file_repository.AcquireNameLock(", sort_names);
            const auto conflict_check = copy_body.find("utils::QueryOccupiedNames(", name_locks);
            const auto ref_increment = copy_body.find("content_service.IncrementRefCountsChecked(", conflict_check);
            const auto insert = copy_body.find("InsertCopiedFiles(", ref_increment);
            const auto quota_commit = copy_body.find("quota_service.CommitReservedToUsed(", insert);
            const auto quota_release = copy_body.find("quota_service.ReleaseReservedStorageChecked(", quota_commit);
            ASSERT_NE(transaction, std::string::npos);
            ASSERT_NE(sort_names, std::string::npos);
            ASSERT_NE(name_locks, std::string::npos);
            ASSERT_NE(conflict_check, std::string::npos);
            ASSERT_NE(ref_increment, std::string::npos);
            ASSERT_NE(insert, std::string::npos);
            ASSERT_NE(quota_commit, std::string::npos);
            ASSERT_NE(quota_release, std::string::npos);
            EXPECT_LT(transaction, sort_names);
            EXPECT_LT(sort_names, name_locks);
            EXPECT_LT(name_locks, conflict_check);
            EXPECT_LT(conflict_check, ref_increment);
            EXPECT_LT(ref_increment, insert);
            EXPECT_LT(insert, quota_commit);
            EXPECT_LT(quota_commit, quota_release);
        }

        TEST(FileMutationServiceCopyContractTest, CopyLocksFolderRootBeforeSubtreeWrites) {
            const auto source = ReadSourceFile("src/services/FileMutationService.cpp");
            const auto copy_body = ExtractCopyBody(source);

            ASSERT_FALSE(copy_body.empty());
            const auto staged_folders = copy_body.find(
                "std::vector<FileIdMapping> staged_folder_mappings;"
            );
            const auto transaction = copy_body.find(
                "auto tx_result = co_await transaction_runner.Run(",
                staged_folders
            );
            const auto name_lock = copy_body.find(
                "m_folder_repository.AcquireNameLock(",
                transaction
            );
            const auto conflict_check = copy_body.find(
                "utils::QueryOccupiedFolderNames(",
                name_lock
            );
            const auto quota_release = copy_body.find(
                "quota_service.ReleaseReservedStorageChecked(",
                conflict_check
            );
            const auto ref_increment = copy_body.find(
                "content_service.IncrementRefCountsChecked(",
                quota_release
            );
            const auto root_insert = copy_body.find(
                "folder_mapper.insert(root_folder)",
                ref_increment
            );
            ASSERT_NE(staged_folders, std::string::npos);
            ASSERT_NE(transaction, std::string::npos);
            ASSERT_NE(name_lock, std::string::npos);
            ASSERT_NE(conflict_check, std::string::npos);
            ASSERT_NE(quota_release, std::string::npos);
            ASSERT_NE(ref_increment, std::string::npos);
            ASSERT_NE(root_insert, std::string::npos);
            EXPECT_LT(transaction, name_lock);
            EXPECT_LT(name_lock, conflict_check);
            EXPECT_LT(conflict_check, quota_release);
            EXPECT_LT(quota_release, ref_increment);
            EXPECT_LT(ref_increment, root_insert);
        }

        TEST(FileMutationServiceCopyContractTest, CopyChecksTargetFolderCountBeforeQuotaCommit) {
            const auto source = ReadSourceFile("src/services/FileMutationService.cpp");
            const auto copy_body = ExtractCopyBody(source);

            ASSERT_FALSE(copy_body.empty());
            const auto staged_folders = copy_body.find(
                "std::vector<FileIdMapping> staged_folder_mappings;"
            );
            const auto root_insert = copy_body.find("folder_mapper.insert(root_folder)", staged_folders);
            const auto target_count = copy_body.find(
                "auto target_count_updated = co_await m_folder_repository.ApplyItemCountDelta(",
                root_insert
            );
            const auto target_count_check = copy_body.find("if (!target_count_updated)", target_count);
            const auto quota_commit = copy_body.find(
                "quota_service.CommitReservedToUsed(",
                target_count_check
            );
            ASSERT_NE(staged_folders, std::string::npos);
            ASSERT_NE(root_insert, std::string::npos);
            ASSERT_NE(target_count, std::string::npos);
            ASSERT_NE(target_count_check, std::string::npos);
            ASSERT_NE(quota_commit, std::string::npos);
            EXPECT_LT(root_insert, target_count);
            EXPECT_LT(target_count, target_count_check);
            EXPECT_LT(target_count_check, quota_commit);
            EXPECT_FALSE(Contains(copy_body, "UPDATE folders SET item_count = item_count + 1"));
        }

        TEST(FileMutationLogContextContractTest, ControllerAndServicesUseExplicitRequestContext) {
            const auto controller_source = ReadSourceFile("src/controllers/FileController.cpp");
            const auto mutation_source = ReadSourceFile("src/services/FileMutationService.cpp");
            const auto trash_source = ReadSourceFile("src/services/TrashService.cpp");

            const auto controller_body = ExtractRange(
                controller_source,
                "auto FileController::Rename(",
                "    auto FileController::Search("
            );
            const auto mutation_body = ExtractRange(
                mutation_source,
                "auto FileMutationService::Rename(",
                "\n} // namespace disk::file"
            );
            const auto trash_body = ExtractRange(
                trash_source,
                "auto TrashService::MoveToTrash(",
                "    auto TrashService::CleanupExpiredTrashItems("
            );

            ASSERT_FALSE(controller_body.empty());
            ASSERT_FALSE(mutation_body.empty());
            ASSERT_FALSE(trash_body.empty());
            EXPECT_EQ(
                CountOccurrences(
                    controller_body,
                    "GetRequestLogContext(request, \"file_mutation\")"
                ),
                4
            );
            EXPECT_TRUE(Contains(controller_body, "m_mutation_service->Rename("));
            EXPECT_TRUE(Contains(controller_body, "m_mutation_service->Move(*parse_result, user_id, log_context)"));
            EXPECT_TRUE(Contains(controller_body, "m_mutation_service->Copy(*parse_result, user_id, log_context)"));
            EXPECT_TRUE(Contains(controller_body, "m_mutation_service->Delete(*parse_result, user_id, log_context)"));
            EXPECT_FALSE(Contains(mutation_body, "IsFilenameExists("));
            EXPECT_TRUE(Contains(mutation_body, "MoveToTrash(std::move(move_request), user_id, log_context)"));
            EXPECT_TRUE(Contains(trash_body, "Logger::Warn(log_context) << \"File not found or delete failed"));

            for (const auto* body : { &controller_body, &mutation_body, &trash_body }) {
                EXPECT_FALSE(Contains(*body, "Logger::Debug()"));
                EXPECT_FALSE(Contains(*body, "Logger::Info()"));
                EXPECT_FALSE(Contains(*body, "Logger::Warn()"));
                EXPECT_FALSE(Contains(*body, "Logger::Error()"));
            }
        }

    } // namespace
} // namespace disk::file
