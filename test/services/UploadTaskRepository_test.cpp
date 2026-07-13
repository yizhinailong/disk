/**
 * @file UploadTaskRepository_test.cpp
 * @brief Upload task repository boundary contract tests
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

#include "services/UploadTaskRepository.hpp"

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

        TEST(UploadTaskRepositoryLookupContractTest, LookupMethodsKeepOwnershipAndResumableGuards) {
            const auto source = ReadSourceFile("src/services/UploadTaskRepository.cpp");

            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::FindById("));
            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::FindByIdForUser("));
            EXPECT_TRUE(Contains(source, "Criteria(UploadTasks::Cols::_id, CompareOperator::EQ, upload_id)"));
            EXPECT_TRUE(Contains(source, "Criteria(UploadTasks::Cols::_user_id, CompareOperator::EQ, user_id)"));

            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::FindInProgressByUserAndHash("));
            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::FindInProgressIdByUserAndHash("));
            EXPECT_TRUE(Contains(source, "WHERE user_id = $1 AND file_hash = $2 AND status = $3 LIMIT 1"));
            EXPECT_TRUE(Contains(source, "disk::upload::UploadTaskStatus::InProgress"));
        }

        TEST(UploadTaskRepositoryStatusTransitionContractTest, TerminalTransitionsAreGuardedByInProgressStatus) {
            const auto source = ReadSourceFile("src/services/UploadTaskRepository.cpp");

            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::MarkCompletedIfInProgress("));
            EXPECT_TRUE(Contains(source, "UPDATE upload_tasks SET status = $1, finalized_at = NOW() WHERE id = $2 AND status = $3"));
            EXPECT_TRUE(Contains(source, "disk::upload::UploadTaskStatus::Completed"));

            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::MarkCancelledIfInProgress("));
            EXPECT_TRUE(Contains(source, "UPDATE upload_tasks SET status = $1, finalized_at = NOW(), fail_reason = $2 "));
            EXPECT_TRUE(Contains(source, "WHERE id = $3 AND status = $4"));
            EXPECT_TRUE(Contains(source, "disk::upload::UploadTaskStatus::Cancelled"));

            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::MarkExpiredIfInProgressReturning("));
            EXPECT_TRUE(Contains(source, "WHERE id = $3 AND status = $4 AND expires_at < NOW() "));
            EXPECT_TRUE(Contains(source, "RETURNING id, temp_path, user_id, reserved_bytes"));
            EXPECT_TRUE(Contains(source, "disk::upload::UploadTaskStatus::Expired"));
        }

        TEST(UploadTaskRepositoryChunkPrimitiveContractTest, ChunkPersistencePrimitivesKeepIdempotencySortingAndCoverage) {
            const auto source = ReadSourceFile("src/services/UploadTaskRepository.cpp");

            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::RecordChunkUploadedIfAbsent("));
            EXPECT_TRUE(Contains(source, "INSERT INTO upload_task_chunks (task_id, chunk_index, uploaded_at) "));
            EXPECT_TRUE(Contains(source, "ON CONFLICT DO NOTHING"));

            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::ListUploadedChunkIndices("));
            EXPECT_TRUE(Contains(source, "SELECT chunk_index FROM upload_task_chunks WHERE task_id = $1 ORDER BY chunk_index"));

            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::GetChunkCoverage("));
            EXPECT_TRUE(Contains(source, "SELECT COUNT(*) AS uploaded_count, "));
            EXPECT_TRUE(Contains(source, "COALESCE(MAX(chunk_index), -1) AS max_chunk_index "));
            EXPECT_TRUE(Contains(source, "FROM upload_task_chunks WHERE task_id = $1"));

            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::DeleteChunks("));
            EXPECT_TRUE(Contains(source, "DELETE FROM upload_task_chunks WHERE task_id = $1"));
        }

        TEST(UploadTaskRepositoryExpirationBoundaryTest, LifecycleDelegatesExpirationSqlToRepository) {
            const auto repository_source = ReadSourceFile("src/services/UploadTaskRepository.cpp");
            const auto lifecycle_source = ReadSourceFile("src/services/UploadLifecycleService.cpp");

            EXPECT_TRUE(Contains(repository_source, "auto UploadTaskRepository::MarkExpiredIfInProgressReturning("));
            EXPECT_TRUE(Contains(repository_source, "UPDATE upload_tasks SET status = $1, finalized_at = NOW(), fail_reason = $2 "));
            EXPECT_TRUE(Contains(repository_source, "WHERE id = $3 AND status = $4 AND expires_at < NOW() "));
            EXPECT_TRUE(Contains(repository_source, "RETURNING id, temp_path, user_id, reserved_bytes"));

            EXPECT_TRUE(Contains(lifecycle_source, "upload_task_repository.MarkExpiredIfInProgressReturning("));
            EXPECT_FALSE(Contains(lifecycle_source, "UPDATE upload_tasks SET status"));
            EXPECT_TRUE(Contains(lifecycle_source, "quota_service.ReleaseReservedStorageChecked("));
            EXPECT_TRUE(Contains(lifecycle_source, "upload_task_repository.DeleteChunks(transaction, upload_id)"));
            EXPECT_TRUE(Contains(lifecycle_source, "CleanupTemp(upload_id)"));
        }

        TEST(UploadTaskRepositoryExpirationBoundaryTest, ReturningExpirationPrimitiveKeepsExpectedSignature) {
            using ExpectedSignature = drogon::Task<std::optional<ExpiredUploadTaskRecord>> (
                UploadTaskRepository::*
            )(
                const drogon::orm::DbClientPtr&,
                const std::string&,
                const std::string&
            ) const;

            static_assert(
                std::is_same_v<
                    decltype(&UploadTaskRepository::MarkExpiredIfInProgressReturning),
                    ExpectedSignature
                >
            );
        }

    } ///< namespace
} ///< namespace disk::file
