/**
 * @file UploadTaskRepository_test.cpp
 * @brief Upload task repository boundary contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include "services/UploadTaskRepository.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

namespace disk::file {
    namespace {

        template <typename Repository>
        concept HasDefaultClientRenewFinalizeLease = requires(
            const Repository& repository,
            const std::string& upload_id,
            const std::string& lease_owner
        ) {
            repository.RenewFinalizeLease(upload_id, uint64_t{}, lease_owner, uint64_t{}, uint32_t{});
        };

        template <typename Repository>
        concept HasExplicitClientRenewFinalizeLease = requires(
            const Repository& repository,
            const drogon::orm::DbClientPtr& client,
            const std::string& upload_id,
            const std::string& lease_owner
        ) {
            repository.RenewFinalizeLease(
                client,
                upload_id,
                uint64_t{},
                lease_owner,
                uint64_t{},
                uint32_t{}
            );
        };

        static_assert(!HasDefaultClientRenewFinalizeLease<UploadTaskRepository>);
        static_assert(HasExplicitClientRenewFinalizeLease<UploadTaskRepository>);

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

            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::FindByIdForUser("));
            EXPECT_TRUE(Contains(source, "Criteria(UploadTasks::Cols::_id, CompareOperator::EQ, upload_id)"));
            EXPECT_TRUE(Contains(source, "Criteria(UploadTasks::Cols::_user_id, CompareOperator::EQ, user_id)"));

            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::FindInProgressByUserAndHash("));
            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::FindInProgressIdByUserAndHash("));
            EXPECT_TRUE(Contains(source, "WHERE user_id = $1 AND file_hash = $2 AND status = $3 LIMIT 1"));
            EXPECT_TRUE(Contains(source, "disk::upload::UploadTaskStatus::InProgress"));
        }

        TEST(UploadTaskRepositorySurfaceContractTest, DeadRepositoryMethodsStayRemoved) {
            const auto header = ReadSourceFile("src/services/UploadTaskRepository.hpp");
            const auto source = ReadSourceFile("src/services/UploadTaskRepository.cpp");
            const auto state_header = ReadSourceFile("src/services/UploadStateMachine.hpp");
            const auto state_source = ReadSourceFile("src/services/UploadStateMachine.cpp");
            const auto lifecycle_header = ReadSourceFile("src/services/UploadLifecycleService.hpp");
            const auto lifecycle_source = ReadSourceFile("src/services/UploadLifecycleService.cpp");

            EXPECT_FALSE(Contains(header, "auto FindById("));
            EXPECT_FALSE(Contains(header, "auto MarkFailedIfLeaseOwned("));
            EXPECT_FALSE(Contains(header, "auto DeleteInProgressById("));
            EXPECT_FALSE(Contains(header, "auto MarkCompleted("));
            EXPECT_FALSE(Contains(header, "auto MarkCompletedIfInProgress("));
            EXPECT_FALSE(Contains(header, "auto GetChunkCoverage("));
            EXPECT_FALSE(Contains(header, "auto DeleteChunks(std::string"));

            EXPECT_FALSE(Contains(source, "UploadTaskRepository::FindById("));
            EXPECT_FALSE(Contains(source, "UploadTaskRepository::MarkFailedIfLeaseOwned("));
            EXPECT_FALSE(Contains(source, "UploadTaskRepository::DeleteInProgressById("));
            EXPECT_FALSE(Contains(source, "UploadTaskRepository::MarkCompleted("));
            EXPECT_FALSE(Contains(source, "UploadTaskRepository::MarkCompletedIfInProgress("));
            EXPECT_FALSE(Contains(source, "UploadTaskRepository::GetChunkCoverage("));
            EXPECT_FALSE(Contains(source, "DeleteChunks(std::string"));

            constexpr std::string_view renew_marker =
                "auto UploadTaskRepository::RenewFinalizeLease(";
            const auto renew_definition = source.find(renew_marker);
            ASSERT_NE(renew_definition, std::string::npos);
            EXPECT_EQ(
                source.find(renew_marker, renew_definition + renew_marker.size()),
                std::string::npos
            );

            EXPECT_FALSE(Contains(lifecycle_header, "struct ChunkCoverage"));
            EXPECT_FALSE(Contains(lifecycle_header, "auto IsCompleteCoverage("));
            EXPECT_FALSE(Contains(lifecycle_source, "auto IsCompleteCoverage("));

            for (const auto marker : {
                     "auto IsAllowedTransition(",
                     "auto CanRenewFinalizeLease(",
                     "auto CanCommitFinalizeLease(",
                     "auto CanComplete(",
                     "auto CanCancelOrExpire(",
                 }) {
                EXPECT_FALSE(Contains(state_header, marker));
            }
            for (const auto marker : {
                     "auto IsAllowedTransition(",
                     "auto CanRenewFinalizeLease(",
                     "auto CanCommitFinalizeLease(",
                     "auto CanComplete(",
                     "auto CanCancelOrExpire(",
                 }) {
                EXPECT_FALSE(Contains(state_source, marker));
            }
            EXPECT_TRUE(Contains(state_header, "auto DecideFinalizeRequest("));
            EXPECT_TRUE(Contains(state_header, "auto DecideCancelRequest("));
            EXPECT_TRUE(Contains(state_header, "auto IsTerminalStatus(UploadTaskStatus status)"));
            EXPECT_FALSE(Contains(state_header, "auto IsTerminalStatus(int status)"));
            EXPECT_FALSE(Contains(state_source, "auto IsTerminalStatus(int status)"));
            EXPECT_TRUE(Contains(source, "disk::upload::DecideFinalizeRequest("));
        }

        TEST(UploadTaskRepositoryStagingContractTest, CreationPersistsImmutableSessionLocation) {
            const auto repository_source = ReadSourceFile("src/services/UploadTaskRepository.cpp");
            const auto lifecycle_source = ReadSourceFile("src/services/UploadLifecycleService.cpp");
            const auto job_contract_source = ReadSourceFile("src/services/StorageJobContract.cpp");

            EXPECT_TRUE(Contains(repository_source, "staging_backend, staging_prefix, status, expires_at"));
            EXPECT_TRUE(Contains(repository_source, "ToStorageValue(staging_session.backend)"));
            EXPECT_TRUE(Contains(repository_source, "staging_session.prefix"));
            EXPECT_TRUE(Contains(repository_source, "auto UploadTaskRepository::FindStagingSessionForUser("));
            EXPECT_TRUE(Contains(repository_source, "COALESCE(staging_prefix, temp_path) AS staging_prefix"));

            EXPECT_TRUE(Contains(lifecycle_source, "config->GetUploadStagingBackend()"));
            EXPECT_TRUE(Contains(lifecycle_source, "config->GetS3StorageConfig().staging_prefix + \"/\" + upload_id"));
            EXPECT_TRUE(Contains(
                repository_source,
                "auto UploadTaskRepository::Create(\n" "        const drogon::orm::DbClientPtr& client,"
            ));
            EXPECT_TRUE(Contains(repository_source, "auto result = co_await client->execSqlCoro("));
            EXPECT_TRUE(Contains(
                lifecycle_source,
                "task = co_await upload_task_repository.Create(\n" "                    transaction,"
            ));
            EXPECT_TRUE(Contains(
                lifecycle_source,
                "disk::file::TransactionRunner transaction_runner(\n" "            m_db_client,\n" "            ErrorInfo(ErrorCode::InternalError, \"Failed to create upload task\")"
            ));
            EXPECT_TRUE(Contains(lifecycle_source, "command.expiry_seconds"));
            EXPECT_TRUE(Contains(lifecycle_source, "FindStagingSessionForUser("));
            EXPECT_TRUE(Contains(lifecycle_source, "BuildStagingCleanupJob("));
            EXPECT_TRUE(Contains(job_contract_source, "payload[\"backend\"]"));
            EXPECT_TRUE(Contains(job_contract_source, "payload[\"prefix\"]"));
            EXPECT_TRUE(Contains(job_contract_source, "\"staging-cleanup:\" + session.upload_id"));
            EXPECT_TRUE(Contains(lifecycle_source, "staging_session.value(),\n            state_version"));

            const auto service_source = ReadSourceFile("src/services/UploadService.cpp");
            EXPECT_TRUE(Contains(service_source, "task.staging_session"));
            EXPECT_TRUE(Contains(
                service_source,
                "auto task_result = co_await FindUploadTask(upload_id, user_id, log_context)"
            ));
            EXPECT_TRUE(Contains(
                service_source,
                "auto staging_session_result = co_await FindUploadStagingSession(\n" "                upload_id,\n" "                user_id,\n" "                log_context\n" "            )"
            ));
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

        TEST(UploadTaskRepositoryStatusTransitionContractTest, CancellationAndExpiryTransitionsAreGuardedByInProgressStatus) {
            const auto source = ReadSourceFile("src/services/UploadTaskRepository.cpp");

            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::MarkCancelledIfInProgressReturning("));
            EXPECT_TRUE(Contains(source, "UPDATE upload_tasks SET status = $1, finalized_at = NOW(), fail_reason = $2, "));
            EXPECT_TRUE(Contains(source, "state_version = state_version + 1 "));
            EXPECT_TRUE(Contains(source, "WHERE id = $3 AND user_id = $4 AND status = $5 AND expires_at >= NOW() "));
            EXPECT_TRUE(Contains(source, "AS staging_prefix, state_version"));
            EXPECT_TRUE(Contains(source, "disk::upload::UploadTaskStatus::Cancelled"));

            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::MarkExpiredIfInProgressReturning("));
            EXPECT_TRUE(Contains(source, "UPDATE upload_tasks SET status = $1, finalized_at = NOW(), fail_reason = $2, "));
            EXPECT_TRUE(Contains(source, "state_version = state_version + 1 "));
            EXPECT_TRUE(Contains(source, "WHERE id = $3 AND status = $4 AND expires_at < NOW() "));
            EXPECT_TRUE(Contains(source, "RETURNING id, temp_path, user_id, reserved_bytes"));
            EXPECT_TRUE(Contains(source, "AS staging_prefix, state_version"));
            EXPECT_TRUE(Contains(source, "disk::upload::UploadTaskStatus::Expired"));
        }

        TEST(UploadTaskRepositoryCancellationContractTest, WinnerAdvancesVersionAndOwnsTransactionalSideEffects) {
            const auto repository_source = ReadSourceFile("src/services/UploadTaskRepository.cpp");
            const auto lifecycle_source = ReadSourceFile("src/services/UploadLifecycleService.cpp");
            const auto service_source = ReadSourceFile("src/services/UploadService.cpp");
            const auto controller_source = ReadSourceFile("src/controllers/FileController.cpp");

            EXPECT_TRUE(Contains(repository_source, "auto UploadTaskRepository::MarkCancelledIfInProgressReturning("));
            EXPECT_TRUE(Contains(repository_source, "RETURNING id, temp_path, user_id, reserved_bytes, staging_backend"));
            EXPECT_TRUE(Contains(repository_source, "AS staging_prefix, state_version"));
            EXPECT_TRUE(Contains(repository_source, "auto UploadTaskRepository::FindCancellationStateByIdForUser("));
            EXPECT_TRUE(Contains(repository_source, "SELECT status, state_version, expires_at < NOW() AS task_expired"));

            const auto transition = lifecycle_source.find("MarkCancelledIfInProgressReturning(");
            const auto quota_release = lifecycle_source.find("ReleaseReservedStorageChecked(", transition);
            const auto cleanup_enqueue = lifecycle_source.find("storage_job_repository.Enqueue(", quota_release);
            const auto chunk_delete = lifecycle_source.find("DeleteChunks(transaction, upload_id)", cleanup_enqueue);
            ASSERT_NE(transition, std::string::npos);
            ASSERT_NE(quota_release, std::string::npos);
            ASSERT_NE(cleanup_enqueue, std::string::npos);
            ASSERT_NE(chunk_delete, std::string::npos);
            EXPECT_LT(transition, quota_release);
            EXPECT_LT(quota_release, cleanup_enqueue);
            EXPECT_LT(cleanup_enqueue, chunk_delete);
            EXPECT_FALSE(Contains(lifecycle_source, "->CleanupSession("));
            EXPECT_TRUE(Contains(lifecycle_source, "DecideCancelRequest(current_state->status)"));
            EXPECT_TRUE(Contains(lifecycle_source, "outcome=success"));
            EXPECT_TRUE(Contains(lifecycle_source, "outcome=replay"));
            EXPECT_FALSE(Contains(service_source, "DecideCancelRequest("));
            EXPECT_TRUE(Contains(service_source, "log_context.operation = \"upload_cancel\""));
            EXPECT_TRUE(Contains(controller_source, "GetRequestLogContext(request, \"upload_cancel\")"));
            EXPECT_TRUE(Contains(controller_source, "log_context.state_version = result.value()"));
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
            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::RecordFinalizeErrorIfLeaseOwned("));
            EXPECT_TRUE(Contains(source, "AND lease_owner = $5 AND state_version = $6 "));
            EXPECT_TRUE(Contains(source, "AND lease_expires_at > NOW()"));
        }

        TEST(UploadTaskRepositoryFinalizeLeaseContractTest, LifecycleClaimsAndRenewsBeforeLeaseGuardedCommit) {
            const auto source = ReadSourceFile("src/services/UploadLifecycleService.cpp");
            const auto claim = source.find("upload_task_repository.ClaimFinalizeLease(");
            const auto assemble = source.find("m_upload_staging_storage->AssembleChunks(");
            const auto first_renew = source.find("RenewFinalizeLease(", assemble);
            const auto transaction_start = source.find("transaction_runner.Run(", first_renew);
            const auto transaction_renew = source.find("RenewFinalizeLease(", transaction_start);
            const auto content_write = source.find("content_service.AcquireReference(", transaction_start);
            const auto guarded_commit = source.find("upload_task_repository.MarkCompletedIfLeaseOwned(");

            ASSERT_NE(claim, std::string::npos);
            ASSERT_NE(assemble, std::string::npos);
            ASSERT_NE(first_renew, std::string::npos);
            ASSERT_NE(transaction_start, std::string::npos);
            ASSERT_NE(transaction_renew, std::string::npos);
            ASSERT_NE(content_write, std::string::npos);
            ASSERT_NE(guarded_commit, std::string::npos);
            EXPECT_LT(claim, assemble);
            EXPECT_LT(assemble, first_renew);
            EXPECT_LT(first_renew, transaction_start);
            EXPECT_LT(transaction_start, transaction_renew);
            EXPECT_LT(transaction_renew, content_write);
            EXPECT_LT(content_write, guarded_commit);
            const auto cleanup_enqueue = source.find("storage_job_repository.Enqueue(", guarded_commit);
            const auto chunk_delete = source.find("DeleteChunks(transaction, command.upload_id)", cleanup_enqueue);
            ASSERT_NE(cleanup_enqueue, std::string::npos);
            ASSERT_NE(chunk_delete, std::string::npos);
            EXPECT_LT(guarded_commit, cleanup_enqueue);
            EXPECT_LT(cleanup_enqueue, chunk_delete);
            EXPECT_TRUE(Contains(source, "completed_file_id"));
            EXPECT_FALSE(Contains(source, "m_blob_store->DeleteBlob(final_storage_path)"));
        }

        TEST(UploadTaskRepositoryChunkPrimitiveContractTest, ChunkPersistencePrimitivesKeepIdempotencySortingAndTransactionalCleanup) {
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

            EXPECT_TRUE(Contains(source, "auto UploadTaskRepository::DeleteChunks("));
            EXPECT_TRUE(Contains(source, "const drogon::orm::DbClientPtr& client"));
            EXPECT_TRUE(Contains(source, "DELETE FROM upload_task_chunks WHERE task_id = $1"));
        }

        TEST(UploadTaskRepositoryExpirationBoundaryTest, LifecycleDelegatesExpirationSqlToRepository) {
            const auto repository_source = ReadSourceFile("src/services/UploadTaskRepository.cpp");
            const auto lifecycle_source = ReadSourceFile("src/services/UploadLifecycleService.cpp");
            const auto service_source = ReadSourceFile("src/services/UploadService.cpp");

            EXPECT_TRUE(Contains(repository_source, "auto UploadTaskRepository::FindUnexpiredByIdForUser("));
            EXPECT_TRUE(Contains(repository_source, "WHERE id = $1 AND user_id = $2 AND expires_at >= NOW()"));
            EXPECT_TRUE(Contains(repository_source, "NOW() + ($14::integer * INTERVAL '1 second')"));
            EXPECT_TRUE(Contains(service_source, "upload_task_repository.FindUnexpiredByIdForUser("));
            EXPECT_FALSE(Contains(service_source, "IsExpired(task.expires_at"));
            EXPECT_FALSE(Contains(lifecycle_source, "task.setExpiresAt("));
            EXPECT_TRUE(Contains(lifecycle_source, "auto expire_result = co_await ExpireInProgressUpload(task_id, log_context)"));
            EXPECT_TRUE(Contains(lifecycle_source, "auto expire_result = co_await ExpireInProgressUpload(task.id, log_context)"));
            EXPECT_TRUE(Contains(lifecycle_source, "if (*expire_result)"));

            EXPECT_TRUE(Contains(repository_source, "auto UploadTaskRepository::MarkExpiredIfInProgressReturning("));
            EXPECT_FALSE(Contains(repository_source, "auto UploadTaskRepository::MarkExpiredIfInProgressBatch("));
            EXPECT_TRUE(Contains(repository_source, "UPDATE upload_tasks SET status = $1, finalized_at = NOW(), fail_reason = $2, "));
            EXPECT_TRUE(Contains(repository_source, "state_version = state_version + 1 "));
            EXPECT_TRUE(Contains(repository_source, "WHERE id = $3 AND status = $4 AND expires_at < NOW() "));
            EXPECT_TRUE(Contains(repository_source, "RETURNING id, temp_path, user_id, reserved_bytes"));
            EXPECT_TRUE(Contains(repository_source, "AS staging_prefix, state_version"));
            EXPECT_TRUE(Contains(repository_source, "ORDER BY expires_at, id LIMIT $2"));

            EXPECT_TRUE(Contains(lifecycle_source, "upload_task_repository.MarkExpiredIfInProgressReturning("));
            EXPECT_FALSE(Contains(lifecycle_source, "UPDATE upload_tasks SET status"));
            EXPECT_TRUE(Contains(lifecycle_source, "quota_service.ReleaseReservedStorageChecked("));
            EXPECT_TRUE(Contains(lifecycle_source, "BuildStagingCleanupJob(expired_record->cleanup.staging_session)"));
            EXPECT_TRUE(Contains(lifecycle_source, "storage_job_repository.Enqueue("));
            EXPECT_TRUE(Contains(lifecycle_source, "upload_task_repository.DeleteChunks(transaction, upload_id)"));
            EXPECT_TRUE(Contains(lifecycle_source, "transitioned_state_version = expired_record->state_version"));
            EXPECT_TRUE(Contains(lifecycle_source, "log_context.state_version = transitioned_state_version.value()"));
            EXPECT_TRUE(Contains(lifecycle_source, "[expire_upload] duration_us="));
            EXPECT_TRUE(Contains(lifecycle_source, "outcome=success"));
            EXPECT_TRUE(Contains(lifecycle_source, "outcome=cas_lost"));
            EXPECT_FALSE(Contains(lifecycle_source, "->CleanupSession("));
        }

        TEST(UploadTaskRepositoryExpirationBoundaryTest, ReturningExpirationPrimitiveKeepsExpectedSignature) {
            using ExpectedSignature = drogon::Task<std::optional<ExpiredUploadTransitionRecord>> (
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
