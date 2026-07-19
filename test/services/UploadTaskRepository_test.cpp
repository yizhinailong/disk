/**
 * @file UploadTaskRepository_test.cpp
 * @brief Upload task repository boundary contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include "services/UploadTaskRepository.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

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

        TEST(UploadTaskRepositoryStagingContractTest, CreationPersistsImmutableSessionLocation) {
            const auto repository_source = ReadSourceFile("src/services/UploadTaskRepository.cpp");
            const auto lifecycle_source = ReadSourceFile("src/services/UploadLifecycleService.cpp");

            EXPECT_TRUE(Contains(repository_source, "staging_backend, staging_prefix, status, expires_at"));
            EXPECT_TRUE(Contains(repository_source, "ToStorageValue(staging_session.backend)"));
            EXPECT_TRUE(Contains(repository_source, "staging_session.prefix"));
            EXPECT_TRUE(Contains(repository_source, "auto UploadTaskRepository::FindStagingSessionForUser("));
            EXPECT_TRUE(Contains(repository_source, "COALESCE(staging_prefix, temp_path) AS staging_prefix"));

            EXPECT_TRUE(Contains(lifecycle_source, "config->GetUploadStagingBackend()"));
            EXPECT_TRUE(Contains(lifecycle_source, "config->GetS3StorageConfig().staging_prefix + \"/\" + upload_id"));
            EXPECT_TRUE(Contains(lifecycle_source, "upload_task_repository.Create(std::move(task), staging_session)"));
            EXPECT_TRUE(Contains(lifecycle_source, "FindStagingSessionForUser("));
            EXPECT_TRUE(Contains(lifecycle_source, "CleanupSession(staging_session.value())"));
            EXPECT_TRUE(Contains(lifecycle_source, "staging_session.value(),\n            state_version"));

            const auto service_source = ReadSourceFile("src/services/UploadService.cpp");
            EXPECT_TRUE(Contains(service_source, "task.staging_session"));
            EXPECT_TRUE(Contains(service_source, "FindUploadStagingSession(upload_id, user_id)"));
        }

        TEST(UploadTaskRepositoryStagingContractTest, BackendStorageValuesRoundTrip) {
            EXPECT_EQ(
                disk::storage::ToStorageValue(disk::storage::UploadStagingBackend::Local),
                "local"
            );
            EXPECT_EQ(
                disk::storage::ToStorageValue(disk::storage::UploadStagingBackend::S3),
                "s3"
            );
            EXPECT_EQ(
                disk::storage::ParseUploadStagingBackend("local"),
                disk::storage::UploadStagingBackend::Local
            );
            EXPECT_EQ(
                disk::storage::ParseUploadStagingBackend("s3"),
                disk::storage::UploadStagingBackend::S3
            );
            EXPECT_FALSE(disk::storage::ParseUploadStagingBackend("filesystem").has_value());
        }

        TEST(UploadTaskRepositoryStatusTransitionContractTest, TerminalTransitionsAreGuardedByInProgressStatus) {
            const auto source = ReadSourceFile("src/services/UploadTaskRepository.cpp");

            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::MarkCompletedIfInProgress("));
            EXPECT_TRUE(Contains(source, "UPDATE upload_tasks SET status = $1, finalized_at = NOW() WHERE id = $2 AND status = $3"));
            EXPECT_TRUE(Contains(source, "disk::upload::UploadTaskStatus::Completed"));

            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::MarkCancelledIfInProgressReturning("));
            EXPECT_TRUE(Contains(source, "UPDATE upload_tasks SET status = $1, finalized_at = NOW(), fail_reason = $2 "));
            EXPECT_TRUE(Contains(source, "WHERE id = $3 AND user_id = $4 AND status = $5 "));
            EXPECT_TRUE(Contains(source, "disk::upload::UploadTaskStatus::Cancelled"));

            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::MarkExpiredIfInProgressReturning("));
            EXPECT_TRUE(Contains(source, "WHERE id = $3 AND status = $4 AND expires_at < NOW() "));
            EXPECT_TRUE(Contains(source, "RETURNING id, temp_path, user_id, reserved_bytes"));
            EXPECT_TRUE(Contains(source, "disk::upload::UploadTaskStatus::Expired"));
        }

        TEST(UploadTaskRepositoryCancellationContractTest, WinnerReleasesQuotaAndChunksInOneTransaction) {
            const auto repository_source = ReadSourceFile("src/services/UploadTaskRepository.cpp");
            const auto lifecycle_source = ReadSourceFile("src/services/UploadLifecycleService.cpp");
            const auto service_source = ReadSourceFile("src/services/UploadService.cpp");

            EXPECT_TRUE(Contains(repository_source, "auto UploadTaskRepository::MarkCancelledIfInProgressReturning("));
            EXPECT_TRUE(Contains(repository_source, "RETURNING id, temp_path, user_id, reserved_bytes, staging_backend"));

            const auto transition = lifecycle_source.find("MarkCancelledIfInProgressReturning(");
            const auto quota_release = lifecycle_source.find("ReleaseReservedStorageChecked(", transition);
            const auto chunk_delete = lifecycle_source.find("DeleteChunks(transaction, upload_id)", quota_release);
            ASSERT_NE(transition, std::string::npos);
            ASSERT_NE(quota_release, std::string::npos);
            ASSERT_NE(chunk_delete, std::string::npos);
            EXPECT_LT(transition, quota_release);
            EXPECT_LT(quota_release, chunk_delete);
            EXPECT_TRUE(Contains(lifecycle_source, "DecideCancelRequest(current_task->getValueOfStatus())"));
            EXPECT_TRUE(Contains(service_source, "DecideCancelRequest(task.getValueOfStatus())"));
        }

        TEST(UploadTaskRepositoryFinalizeLeaseContractTest, LeaseMutationsUseDatabaseTimeOwnerAndVersion) {
            const auto source = ReadSourceFile("src/services/UploadTaskRepository.cpp");

            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::ClaimFinalizeLease("));
            EXPECT_TRUE(Contains(source, "task.expires_at >= NOW()"));
            EXPECT_TRUE(Contains(source, "task.lease_expires_at <= NOW()"));
            EXPECT_TRUE(Contains(source, "COUNT(*) = task.total_chunks"));
            EXPECT_TRUE(Contains(source, "state_version = state_version + 1"));
            EXPECT_TRUE(Contains(source, "finalize_attempts = finalize_attempts + 1"));

            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::RenewFinalizeLease("));
            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::MarkCompletedIfLeaseOwned("));
            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::MarkFailedIfLeaseOwned("));
            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::RecordFinalizeErrorIfLeaseOwned("));
            EXPECT_TRUE(Contains(source, "AND lease_owner = $5 AND state_version = $6 "));
            EXPECT_TRUE(Contains(source, "AND lease_expires_at > NOW()"));
        }

        TEST(UploadTaskRepositoryFinalizeLeaseContractTest, LifecycleClaimsAndRenewsBeforeLeaseGuardedCommit) {
            const auto source = ReadSourceFile("src/services/UploadLifecycleService.cpp");
            const auto claim = source.find("upload_task_repository.ClaimFinalizeLease(");
            const auto assemble = source.find("m_upload_staging_storage->AssembleChunks(");
            const auto first_renew = source.find("RenewFinalizeLease(", assemble);
            const auto guarded_commit = source.find("upload_task_repository.MarkCompletedIfLeaseOwned(");

            ASSERT_NE(claim, std::string::npos);
            ASSERT_NE(assemble, std::string::npos);
            ASSERT_NE(first_renew, std::string::npos);
            ASSERT_NE(guarded_commit, std::string::npos);
            EXPECT_LT(claim, assemble);
            EXPECT_LT(assemble, first_renew);
            EXPECT_LT(first_renew, guarded_commit);
            EXPECT_TRUE(Contains(source, "completed_file_id"));
            EXPECT_FALSE(Contains(source, "m_blob_store->DeleteBlob(final_storage_path)"));
        }

        TEST(UploadTaskRepositoryChunkPrimitiveContractTest, ChunkPersistencePrimitivesKeepIdempotencySortingAndCoverage) {
            const auto source = ReadSourceFile("src/services/UploadTaskRepository.cpp");

            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::RecordChunkIfInProgress("));
            EXPECT_TRUE(Contains(source, "WITH eligible_task AS MATERIALIZED ("));
            EXPECT_TRUE(Contains(source, "AND status = $3 AND expires_at >= NOW() "));
            EXPECT_TRUE(Contains(source, "FOR UPDATE"));
            EXPECT_TRUE(Contains(source, "(task_id, chunk_index, size_bytes, hash_md5, object_key, etag, uploaded_at) "));
            EXPECT_TRUE(Contains(source, "ON CONFLICT (task_id, chunk_index) DO UPDATE SET "));
            EXPECT_TRUE(Contains(source, "hash_md5 = COALESCE(upload_task_chunks.hash_md5, EXCLUDED.hash_md5)"));
            EXPECT_TRUE(Contains(source, "object_key = COALESCE(upload_task_chunks.object_key, EXCLUDED.object_key)"));
            EXPECT_TRUE(Contains(source, "ChunkRecordDisposition::TaskRejected"));
            EXPECT_TRUE(Contains(source, "ChunkRecordDisposition::MetadataConflict"));

            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::ListChunksForAssembly("));
            EXPECT_TRUE(Contains(source, "SELECT chunk_index, size_bytes, hash_md5, object_key, etag "));
            EXPECT_TRUE(Contains(source, "FROM upload_task_chunks WHERE task_id = $1 ORDER BY chunk_index"));

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
            EXPECT_TRUE(Contains(lifecycle_source, "CleanupSession(staging_session.value())"));
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
                    ExpectedSignature>
            );
        }

    } // namespace
} // namespace disk::file
