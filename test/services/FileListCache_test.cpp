/**
 * @file FileListCache_test.cpp
 * @brief File-list cache generation integration contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

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

        auto CountOccurrences(const std::string& source, const std::string& expected) -> size_t {
            size_t count = 0;
            size_t offset = 0;
            while ((offset = source.find(expected, offset)) != std::string::npos) {
                ++count;
                offset += expected.size();
            }
            return count;
        }

        TEST(FileListCacheContractTest, QueryUsesGenerationAndSkipsWritesWhenRedisIsUnavailable) {
            const auto source = ReadSourceFile("src/services/FileQueryService.cpp");

            EXPECT_NE(source.find("FileListCache::GetVersion(m_redis_service, user_id)"), std::string::npos);
            EXPECT_NE(source.find("*version_result,"), std::string::npos);
            EXPECT_NE(source.find("if (cache_available)"), std::string::npos);
            EXPECT_NE(source.find("cache_available = false;"), std::string::npos);
            EXPECT_NE(source.find("falling back to database"), std::string::npos);
            EXPECT_NE(source.find("ErrorCode::InternalError,\n                \"Failed to query file list\""), std::string::npos);
        }

        TEST(FileListCacheContractTest, EveryDriveMutationBoundaryBumpsTheSharedGeneration) {
            const auto upload = ReadSourceFile("src/services/UploadService.cpp");
            const auto mutation = ReadSourceFile("src/services/FileMutationService.cpp");
            const auto folder = ReadSourceFile("src/services/FolderService.cpp");
            const auto trash = ReadSourceFile("src/services/TrashService.cpp");
            const auto share = ReadSourceFile("src/services/ShareService.cpp");

            EXPECT_GE(CountOccurrences(upload, "FileListCache::Invalidate(m_redis_service, user_id)"), 2);
            EXPECT_GE(CountOccurrences(mutation, "FileListCache::Invalidate(m_redis_service, user_id)"), 4);
            EXPECT_GE(CountOccurrences(folder, "FileListCache::Invalidate(m_redis_service, user_id)"), 2);
            EXPECT_GE(CountOccurrences(trash, "FileListCache::Invalidate(m_redis_service, user_id)"), 2);
            EXPECT_GE(CountOccurrences(share, "FileListCache::Invalidate(m_redis_service, target_user_id)"), 1);
        }

        TEST(FileListCacheContractTest, TransactionalWritersCommitBeforeBumpingGeneration) {
            const auto folder = ReadSourceFile("src/services/FolderService.cpp");
            const auto share = ReadSourceFile("src/services/ShareService.cpp");

            const auto folder_commit = folder.find("TransactionRunner::Commit(txn)");
            const auto folder_invalidation = folder.find(
                "FileListCache::Invalidate(m_redis_service, user_id)",
                folder_commit
            );
            ASSERT_NE(folder_commit, std::string::npos);
            ASSERT_NE(folder_invalidation, std::string::npos);
            EXPECT_LT(folder_commit, folder_invalidation);

            const auto save_to_drive = share.find("auto ShareService::SaveToDrive(");
            const auto share_commit = share.find("TransactionRunner::Commit(transaction)", save_to_drive);
            const auto share_invalidation = share.find(
                "FileListCache::Invalidate(m_redis_service, target_user_id)",
                save_to_drive
            );
            ASSERT_NE(save_to_drive, std::string::npos);
            ASSERT_NE(share_commit, std::string::npos);
            ASSERT_NE(share_invalidation, std::string::npos);
            EXPECT_LT(share_commit, share_invalidation);
        }

    } // namespace
} // namespace disk::file
