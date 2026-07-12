/**
 * @file FileRepository_test.cpp
 * @brief File repository boundary contract tests
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

#include "services/FileRepository.hpp"

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

        TEST(FileRepositorySignatureContractTest, ExposesTransactionAwarePersistencePrimitives) {
            using FindOwnedSignature = drogon::Task<std::optional<drogon_model::disk::Files>> (
                FileRepository::*
            )(const drogon::orm::DbClientPtr&, uint64_t, uint64_t) const;
            using FetchOwnedSignature = drogon::Task<std::vector<drogon_model::disk::Files>> (
                FileRepository::*
            )(const drogon::orm::DbClientPtr&, const std::vector<uint64_t>&, uint64_t) const;
            using RenameSignature = drogon::Task<bool> (FileRepository::*)(
                const drogon::orm::DbClientPtr&,
                uint64_t,
                uint64_t,
                const std::string&,
                const std::string&,
                const std::string&,
                const trantor::Date&
            ) const;

            static_assert(std::is_same_v<decltype(&FileRepository::FindOwnedFile), FindOwnedSignature>);
            static_assert(std::is_same_v<decltype(&FileRepository::FetchOwnedFilesByIds), FetchOwnedSignature>);
            static_assert(std::is_same_v<decltype(&FileRepository::RenameOwnedFile), RenameSignature>);
        }

        TEST(FileRepositorySqlContractTest, OwnershipQueriesStayUserScopedAndBatchSafe) {
            const auto source = ReadSourceFile("src/services/FileRepository.cpp");

            EXPECT_TRUE(Contains(source, "auto FileRepository::FindOwnedFile("));
            EXPECT_TRUE(Contains(source, "FROM files WHERE id = $1 AND user_id = $2"));

            EXPECT_TRUE(Contains(source, "auto FileRepository::FetchOwnedFilesByIds("));
            EXPECT_TRUE(Contains(source, "BatchUtils::Chunk(file_ids, DEFAULT_BATCH_CHUNK_SIZE)"));
            EXPECT_TRUE(Contains(source, "BatchUtils::BuildSafeNumericInClause(chunk)"));
            EXPECT_TRUE(Contains(source, ") AND user_id = $1 ORDER BY id ASC"));
        }

        TEST(FileRepositorySqlContractTest, MoveAndRenameUpdatesKeepUserPredicates) {
            const auto source = ReadSourceFile("src/services/FileRepository.cpp");

            EXPECT_TRUE(Contains(source, "auto FileRepository::RenameOwnedFile("));
            EXPECT_TRUE(Contains(source, "UPDATE files SET name = $1, extension = $2, path = $3, updated_at = $4 "));
            EXPECT_TRUE(Contains(source, "WHERE id = $5 AND user_id = $6"));

            EXPECT_TRUE(Contains(source, "auto FileRepository::UpdateFileLocation("));
            EXPECT_TRUE(Contains(source, "UPDATE files SET folder_id = $1, path = $2, updated_at = $3 "));
            EXPECT_TRUE(Contains(source, "WHERE id = $4 AND user_id = $5"));

            EXPECT_TRUE(Contains(source, "auto FileRepository::UpdateFilePath("));
            EXPECT_TRUE(Contains(source, "WHERE id = $3 AND user_id = $4"));
        }

        TEST(FileRepositorySqlContractTest, DescendantPathUpdateSqlRemainsNamedAndVisible) {
            const auto source = ReadSourceFile("src/services/FileRepository.cpp");

            EXPECT_TRUE(Contains(source, "kUpdateDescendantFilePathsForFolderMoveSql"));
            EXPECT_TRUE(Contains(source, "SUBSTRING(path FROM LENGTH($2) + 1)"));
            EXPECT_TRUE(Contains(source, "WHERE user_id = $4 AND path LIKE $2 || '%'"));
        }

    } ///< namespace
} ///< namespace disk::file
