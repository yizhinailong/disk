/**
 * @file FileRepository_test.cpp
 * @brief File repository boundary contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include <cstddef>
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

        auto CountOccurrences(const std::string& source, const std::string& expected) -> std::size_t {
            std::size_t count = 0;
            std::size_t offset = 0;
            while ((offset = source.find(expected, offset)) != std::string::npos) {
                ++count;
                offset += expected.size();
            }
            return count;
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
            static_assert(std::is_default_constructible_v<FileRepository>);
            static_assert(std::is_empty_v<FileRepository>);
        }

        TEST(FileRepositorySignatureContractTest, CarriesNoUnusedDefaultClientState) {
            const auto header = ReadSourceFile("src/services/FileRepository.hpp");
            const auto source = ReadSourceFile("src/services/FileRepository.cpp");
            const auto folder_repository = ReadSourceFile("src/services/FolderRepository.cpp");
            const auto folder_service = ReadSourceFile("src/services/FolderService.cpp");
            const auto file_mutation_service = ReadSourceFile("src/services/FileMutationService.cpp");

            EXPECT_EQ(CountOccurrences(header, "const drogon::orm::DbClientPtr& client,"), 6U);
            EXPECT_EQ(CountOccurrences(source, "const drogon::orm::DbClientPtr& client,"), 6U);
            EXPECT_FALSE(Contains(header, "m_db_client"));
            EXPECT_FALSE(Contains(header, "FileRepository(drogon::orm::DbClientPtr"));
            EXPECT_FALSE(Contains(source, "FileRepository::FileRepository("));
            EXPECT_FALSE(Contains(folder_repository, "FileRepository file_repository(m_db_client)"));
            EXPECT_FALSE(Contains(folder_service, "FileRepository file_repository(m_db_client)"));
            EXPECT_FALSE(Contains(file_mutation_service, "m_file_repository(m_db_client)"));
            EXPECT_EQ(CountOccurrences(folder_repository, "FileRepository file_repository;"), 2U);
            EXPECT_TRUE(Contains(folder_service, "FileRepository file_repository;"));
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

        TEST(FileRepositorySqlContractTest, UnusedDescendantPathUpdatePrimitiveDoesNotReturn) {
            const auto header = ReadSourceFile("src/services/FileRepository.hpp");
            const auto source = ReadSourceFile("src/services/FileRepository.cpp");
            const auto folder_service = ReadSourceFile("src/services/FolderService.cpp");
            const auto file_mutation_service = ReadSourceFile("src/services/FileMutationService.cpp");

            EXPECT_FALSE(Contains(header, "UpdateDescendantFilePathsForFolderMove"));
            EXPECT_FALSE(Contains(source, "UpdateDescendantFilePathsForFolderMove"));
            EXPECT_FALSE(Contains(source, "SUBSTRING(path FROM LENGTH($2) + 1)"));
            EXPECT_TRUE(Contains(source, "auto FileRepository::UpdateFilePath("));
            EXPECT_TRUE(Contains(folder_service, "file_repository.UpdateFilePath("));
            EXPECT_TRUE(Contains(file_mutation_service, "m_file_repository.UpdateFilePath("));
            EXPECT_TRUE(Contains(folder_service, "BuildFilePath(path_it->second, file.getValueOfName())"));
            EXPECT_TRUE(Contains(file_mutation_service, "utils::BuildFilePath(path_it->second, file.getValueOfName())"));
        }

    } ///< namespace
} ///< namespace disk::file
