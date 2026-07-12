/**
 * @file FolderRepository_test.cpp
 * @brief Folder repository boundary contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "services/FolderRepository.hpp"

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

    } ///< namespace
} ///< namespace disk::folder
