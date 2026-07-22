/**
 * @file FolderRepository_test.cpp
 * @brief Folder repository boundary contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include "services/FolderRepository.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

namespace disk::folder {
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

        auto EveryCallContainsContext(
            const std::string& source,
            std::string_view marker,
            size_t expected_count
        ) -> bool {
            size_t count = 0;
            size_t position = 0;
            while ((position = source.find(marker, position)) != std::string::npos) {
                const auto end = source.find(");", position);
                if (end == std::string::npos ||
                    source.substr(position, end - position).find("log_context") ==
                        std::string::npos) {
                    return false;
                }
                ++count;
                position = end + 2;
            }
            return count == expected_count;
        }

        TEST(FolderRepositorySignatureContractTest, ExposesTransactionAwareOwnershipAndSubtreePrimitives) {
            using FindOwnedSignature = drogon::Task<std::optional<drogon_model::disk::Folders>> (
                FolderRepository::*
            )(const drogon::orm::DbClientPtr&, uint64_t, uint64_t) const;
            using SubtreeSignature = drogon::Task<std::vector<drogon_model::disk::Folders>> (
                FolderRepository::*
            )(const drogon::orm::DbClientPtr&, uint64_t, uint64_t) const;
            using BatchPlanSignature = drogon::Task<std::unordered_map<uint64_t, FolderDeletePlan>> (
                FolderRepository::*
            )(const drogon::orm::DbClientPtr&, const std::vector<uint64_t>&, uint64_t) const;

            static_assert(std::is_same_v<decltype(&FolderRepository::FindOwnedFolder), FindOwnedSignature>);
            static_assert(std::is_same_v<decltype(&FolderRepository::FetchFolderSubtree), SubtreeSignature>);
            static_assert(std::is_same_v<decltype(&FolderRepository::FetchBatchFolderDeletePlans), BatchPlanSignature>);
        }

        TEST(FolderRepositorySqlContractTest, OwnershipAndLocationQueriesStayUserScoped) {
            const auto source = ReadSourceFile("src/services/FolderRepository.cpp");

            EXPECT_TRUE(Contains(source, "auto FolderRepository::FindOwnedFolder("));
            EXPECT_TRUE(Contains(source, "FROM folders WHERE id = $1 AND user_id = $2"));

            EXPECT_TRUE(Contains(source, "auto FolderRepository::ResolveOwnedFolderLocation("));
            EXPECT_TRUE(Contains(source, "SELECT path, depth FROM folders WHERE id = $1 AND user_id = $2"));
        }

        TEST(FolderRepositorySqlContractTest, RecursiveSqlIsNamedVisibleAndUserScoped) {
            const auto source = ReadSourceFile("src/services/FolderRepository.cpp");

            EXPECT_TRUE(Contains(source, "kFetchFolderSubtreeSql"));
            EXPECT_TRUE(Contains(source, "WITH RECURSIVE folder_tree AS"));
            EXPECT_TRUE(Contains(source, "FROM folders WHERE id = $1 AND user_id = $2"));
            EXPECT_TRUE(Contains(source, "WHERE f.user_id = $2"));

            EXPECT_TRUE(Contains(source, "kFetchBatchFolderSubtreeSqlPrefix"));
            EXPECT_TRUE(Contains(source, "id AS root_id"));
            EXPECT_TRUE(Contains(source, "BatchUtils::BuildSafeNumericInClause(folder_ids)"));

            EXPECT_TRUE(Contains(source, "kFetchFolderTreeRowsSql"));
            EXPECT_TRUE(Contains(source, "kFetchBreadcrumbRowsSql"));
            EXPECT_TRUE(Contains(source, "WHERE f.user_id = $2"));
        }

        TEST(FolderRepositorySqlContractTest, PathAndCountUpdatesRemainPersistenceOnly) {
            const auto source = ReadSourceFile("src/services/FolderRepository.cpp");

            EXPECT_TRUE(Contains(source, "auto FolderRepository::RenameFolderPath("));
            EXPECT_TRUE(Contains(source, "UPDATE folders SET name = $1, path = $2, updated_at = $3 "));
            EXPECT_TRUE(Contains(source, "WHERE id = $4 AND user_id = $5"));

            EXPECT_TRUE(Contains(source, "auto FolderRepository::UpdateFolderLocationForMove("));
            EXPECT_TRUE(Contains(source, "parent_id = $1, path = $2, depth = depth + $3"));
            EXPECT_TRUE(Contains(source, "WHERE id = $5 AND user_id = $6"));

            EXPECT_TRUE(Contains(source, "auto FolderRepository::ApplyItemCountDelta("));
            EXPECT_TRUE(Contains(source, "GREATEST(item_count + $1, 0)"));
            EXPECT_TRUE(Contains(source, "WHERE id = $3 AND user_id = $4"));

            EXPECT_FALSE(Contains(source, "DeleteByPrefix"));
            EXPECT_FALSE(Contains(source, "newTransactionCoro"));
        }

        TEST(FolderRepositoryLogContextContractTest, SharedPersistenceFailuresKeepCallerContext) {
            const auto repository_header = ReadSourceFile("src/services/FolderRepository.hpp");
            const auto repository_source = ReadSourceFile("src/services/FolderRepository.cpp");
            const auto utils_header = ReadSourceFile("src/services/FileServiceUtils.hpp");
            const auto utils_source = ReadSourceFile("src/services/FileServiceUtils.cpp");
            const auto upload_lifecycle =
                ReadSourceFile("src/services/UploadLifecycleService.cpp");
            const auto file_mutation =
                ReadSourceFile("src/services/FileMutationService.cpp");
            const auto trash_header = ReadSourceFile("src/services/TrashService.hpp");
            const auto trash_source = ReadSourceFile("src/services/TrashService.cpp");

            ASSERT_FALSE(repository_header.empty());
            ASSERT_FALSE(repository_source.empty());
            ASSERT_FALSE(utils_header.empty());
            ASSERT_FALSE(utils_source.empty());
            ASSERT_FALSE(upload_lifecycle.empty());
            ASSERT_FALSE(file_mutation.empty());
            ASSERT_FALSE(trash_header.empty());
            ASSERT_FALSE(trash_source.empty());

            EXPECT_EQ(
                CountOccurrences(
                    repository_header,
                    "disk::utils::LogContext log_context = {}"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(utils_header, "disk::utils::LogContext log_context = {}"),
                4U
            );
            EXPECT_TRUE(Contains(
                trash_header,
                "disk::utils::LogContext log_context = {}"
            ));

            EXPECT_EQ(CountOccurrences(repository_source, "Logger::Warn(log_context)"), 1U);
            EXPECT_EQ(CountOccurrences(utils_source, "Logger::Warn(log_context)"), 3U);
            EXPECT_FALSE(Contains(repository_source, "Logger::Warn()"));
            EXPECT_FALSE(Contains(utils_source, "Logger::Warn()"));
            EXPECT_TRUE(Contains(
                repository_source,
                "Logger::Warn(log_context) << \"Folder location lookup failed\";"
            ));
            EXPECT_TRUE(Contains(
                utils_source,
                "Logger::Warn(log_context) << \"Trash record batch insert failed\";"
            ));
            EXPECT_TRUE(Contains(
                utils_source,
                "Logger::Warn(log_context) << \"File batch delete failed\";"
            ));
            EXPECT_TRUE(Contains(
                utils_source,
                "Logger::Warn(log_context) << \"Folder batch delete failed\";"
            ));

            EXPECT_TRUE(EveryCallContainsContext(
                utils_source,
                "co_return co_await repository.ResolveOwnedFolderLocation(",
                1U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                upload_lifecycle,
                "disk::file::utils::ResolveFolderLocation(",
                2U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                file_mutation,
                "m_folder_repository.ResolveOwnedFolderLocation(",
                2U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                file_mutation,
                "utils::ResolveFolderLocation(",
                2U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                trash_source,
                "disk::file::utils::InsertTrashRecords(",
                1U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                trash_source,
                "co_await CreateTrashRecords(",
                1U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                trash_source,
                "disk::file::utils::DeleteFilesByIds(",
                1U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                trash_source,
                "disk::file::utils::DeleteFoldersByIds(",
                1U
            ));

            for (const auto* source : { &repository_source, &utils_source }) {
                EXPECT_FALSE(Contains(*source, "log_context."));
                EXPECT_FALSE(Contains(*source, ".what()"));
                EXPECT_FALSE(Contains(*source, "Authorization"));
            }
            EXPECT_FALSE(Contains(repository_source, "folder_id="));
            EXPECT_FALSE(Contains(utils_source, "Batch trash insert failed"));
        }

    } // namespace
} // namespace disk::folder
