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

        auto ExtractMoveBody(const std::string& source) -> std::string {
            const auto begin = source.find("auto FileMutationService::Move(");
            const auto end = source.find("    /// ==================== Copy ====================", begin);
            if (begin == std::string::npos || end == std::string::npos) {
                return {};
            }
            return source.substr(begin, end - begin);
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

        TEST(FileMutationServiceMoveContractTest, MovePreservesValidationAndCacheBoundaries) {
            const auto source = ReadSourceFile("src/services/FileMutationService.cpp");
            const auto move_body = ExtractMoveBody(source);

            ASSERT_FALSE(move_body.empty());
            EXPECT_TRUE(Contains(move_body, "Cannot move a folder into itself or its descendant"));
            EXPECT_TRUE(Contains(move_body, "co_return std::unexpected(ErrorInfo("));
            EXPECT_TRUE(Contains(move_body, "Preserve existing target-folder-only file list cache invalidation"));
            EXPECT_TRUE(Contains(move_body, "co_await InvalidateFileListCache(user_id, { request.target_folder_id });"));

            const auto result_check = move_body.find("if (!transaction_result)");
            const auto cache_invalidation = move_body.find("InvalidateFileListCache");
            ASSERT_NE(result_check, std::string::npos);
            ASSERT_NE(cache_invalidation, std::string::npos);
            EXPECT_LT(result_check, cache_invalidation);
        }

    } // namespace
} // namespace disk::file
