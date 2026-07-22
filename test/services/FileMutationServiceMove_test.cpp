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

        TEST(FileMutationServiceMoveContractTest, MoveUsesTransactionRunnerBoundary) {
            const auto source = ReadSourceFile("src/services/FileMutationService.cpp");
            const auto move_body = ExtractMoveBody(source);

            ASSERT_FALSE(move_body.empty());
            EXPECT_TRUE(Contains(source, "#include \"TransactionRunner.hpp\""));
            EXPECT_TRUE(Contains(move_body, "TransactionRunner transaction_runner("));
            EXPECT_TRUE(Contains(move_body, "transaction_runner.Run("));
            EXPECT_TRUE(Contains(move_body, "ErrorInfo(ErrorCode::InternalError, \"Failed to move items\")"));

            EXPECT_FALSE(Contains(move_body, "newTransactionCoro"));
            EXPECT_FALSE(Contains(move_body, "txn->rollback"));
            EXPECT_FALSE(Contains(move_body, "ServiceValidationException"));
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
            EXPECT_TRUE(Contains(mutation_body, "IsFilenameExists(folder_id, new_name, user_id, log_context)"));
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
