/**
 * @file FileListCache_test.cpp
 * @brief File-list cache generation integration contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include <array>
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

            EXPECT_NE(
                source.find("FileListCache::GetVersion(m_redis_service, user_id, log_context)"),
                std::string::npos
            );
            EXPECT_NE(source.find("*version_result,"), std::string::npos);
            EXPECT_NE(source.find("if (cache_available)"), std::string::npos);
            EXPECT_NE(source.find("cache_available = false;"), std::string::npos);
            EXPECT_NE(source.find("falling back to database"), std::string::npos);
            EXPECT_NE(source.find("ErrorCode::InternalError,\n                \"Failed to query file list\""), std::string::npos);
        }

        TEST(FileQueryLogContextContractTest, ControllerAndServiceUseExplicitRequestContext) {
            const auto controller = ReadSourceFile("src/controllers/FileController.cpp");
            const auto service = ReadSourceFile("src/services/FileQueryService.cpp");

            const auto list_controller_begin = controller.find("auto FileController::List(");
            const auto detail_controller_begin = controller.find("auto FileController::GetDetail(");
            const auto download_controller_begin = controller.find("auto FileController::DownloadInfo(");
            const auto search_controller_begin = controller.find("auto FileController::Search(");
            const auto controller_end = controller.find("} // namespace disk::file", search_controller_begin);
            ASSERT_NE(list_controller_begin, std::string::npos);
            ASSERT_NE(detail_controller_begin, std::string::npos);
            ASSERT_NE(download_controller_begin, std::string::npos);
            ASSERT_NE(search_controller_begin, std::string::npos);
            ASSERT_NE(controller_end, std::string::npos);

            const auto list_service_begin = service.find("auto FileQueryService::GetFileList(");
            const auto detail_service_begin = service.find("auto FileQueryService::GetFileDetail(");
            const auto download_service_begin = service.find("auto FileQueryService::GetDownloadInfo(");
            const auto search_service_begin = service.find("auto FileQueryService::Search(");
            const auto service_end = service.find("} // namespace disk::file", search_service_begin);
            ASSERT_NE(list_service_begin, std::string::npos);
            ASSERT_NE(detail_service_begin, std::string::npos);
            ASSERT_NE(download_service_begin, std::string::npos);
            ASSERT_NE(search_service_begin, std::string::npos);
            ASSERT_NE(service_end, std::string::npos);

            const std::array<std::string_view, 6> sections{
                std::string_view(controller).substr(list_controller_begin, detail_controller_begin - list_controller_begin),
                std::string_view(controller).substr(detail_controller_begin, download_controller_begin - detail_controller_begin),
                std::string_view(controller).substr(search_controller_begin, controller_end - search_controller_begin),
                std::string_view(service).substr(
                    list_service_begin,
                    detail_service_begin - list_service_begin
                ),
                std::string_view(service).substr(
                    detail_service_begin,
                    download_service_begin - detail_service_begin
                ),
                std::string_view(service).substr(
                    search_service_begin,
                    service_end - search_service_begin
                ),
            };
            for (const auto section : sections) {
                EXPECT_EQ(section.find("Logger::Debug()"), std::string_view::npos);
                EXPECT_EQ(section.find("Logger::Info()"), std::string_view::npos);
                EXPECT_EQ(section.find("Logger::Warn()"), std::string_view::npos);
                EXPECT_EQ(section.find("Logger::Error()"), std::string_view::npos);
                EXPECT_NE(section.find("log_context"), std::string_view::npos);
            }

            EXPECT_EQ(
                CountOccurrences(
                    controller,
                    "GetRequestLogContext(request, \"file_query\")"
                ),
                3
            );
            EXPECT_NE(
                controller.find("GetFileList(*parse_result, user_id, log_context)"),
                std::string::npos
            );
            EXPECT_NE(
                controller.find("m_query_service->Search(*parse_result, user_id, log_context)"),
                std::string::npos
            );
        }

        TEST(FileListCacheContractTest, EveryDriveMutationBoundaryBumpsTheSharedGeneration) {
            const auto upload = ReadSourceFile("src/services/UploadService.cpp");
            const auto mutation = ReadSourceFile("src/services/FileMutationService.cpp");
            const auto folder = ReadSourceFile("src/services/FolderService.cpp");
            const auto trash = ReadSourceFile("src/services/TrashService.cpp");
            const auto share = ReadSourceFile("src/services/ShareService.cpp");

            EXPECT_GE(CountOccurrences(upload, "FileListCache::Invalidate("), 2);
            EXPECT_GE(CountOccurrences(mutation, "FileListCache::Invalidate("), 4);
            EXPECT_GE(CountOccurrences(folder, "FileListCache::Invalidate("), 2);
            EXPECT_GE(CountOccurrences(trash, "FileListCache::Invalidate("), 2);
            EXPECT_GE(CountOccurrences(share, "FileListCache::Invalidate("), 1);
        }

        TEST(FileListCacheContractTest, TransactionalWritersCommitBeforeBumpingGeneration) {
            const auto folder = ReadSourceFile("src/services/FolderService.cpp");
            const auto mutation = ReadSourceFile("src/services/FileMutationService.cpp");
            const auto share = ReadSourceFile("src/services/ShareService.cpp");

            const auto folder_rename = folder.find("auto FolderService::Rename(");
            const auto folder_transaction = folder.find(
                "rename_transaction_runner.Run(",
                folder_rename
            );
            const auto folder_result_check = folder.find(
                "if (!transaction_result)",
                folder_transaction
            );
            const auto folder_invalidation = folder.find(
                "FileListCache::Invalidate(",
                folder_result_check
            );
            ASSERT_NE(folder_rename, std::string::npos);
            ASSERT_NE(folder_transaction, std::string::npos);
            ASSERT_NE(folder_result_check, std::string::npos);
            ASSERT_NE(folder_invalidation, std::string::npos);
            EXPECT_LT(folder_transaction, folder_result_check);
            EXPECT_LT(folder_result_check, folder_invalidation);

            const auto file_rename = mutation.find("auto FileMutationService::Rename(");
            const auto file_transaction = mutation.find(
                "rename_transaction_runner.Run(",
                file_rename
            );
            const auto file_result_check = mutation.find(
                "if (!transaction_result)",
                file_transaction
            );
            const auto file_invalidation = mutation.find(
                "FileListCache::Invalidate(",
                file_result_check
            );
            ASSERT_NE(file_rename, std::string::npos);
            ASSERT_NE(file_transaction, std::string::npos);
            ASSERT_NE(file_result_check, std::string::npos);
            ASSERT_NE(file_invalidation, std::string::npos);
            EXPECT_LT(file_transaction, file_result_check);
            EXPECT_LT(file_result_check, file_invalidation);

            const auto save_to_drive = share.find("auto ShareService::SaveToDrive(");
            const auto share_commit = share.find("TransactionRunner::Commit(", save_to_drive);
            const auto share_commit_context = share.find("log_context", share_commit);
            const auto share_invalidation = share.find(
                "FileListCache::Invalidate(",
                save_to_drive
            );
            ASSERT_NE(save_to_drive, std::string::npos);
            ASSERT_NE(share_commit, std::string::npos);
            ASSERT_NE(share_commit_context, std::string::npos);
            ASSERT_NE(share_invalidation, std::string::npos);
            EXPECT_LT(share_commit_context, share_invalidation);
            EXPECT_LT(share_commit, share_invalidation);
        }

    } // namespace
} // namespace disk::file
