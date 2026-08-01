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
#include <json/reader.h>
#include <trantor/utils/Date.h>

#include "services/FileRepository.hpp"
#include "services/FileServiceUtils.hpp"

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

        TEST(FileExtensionContractTest, ActiveFileWritersUseSharedExtraction) {
            const auto utils_header = ReadSourceFile("src/services/FileServiceUtils.hpp");
            const auto utils_source = ReadSourceFile("src/services/FileServiceUtils.cpp");
            const auto upload_source = ReadSourceFile("src/services/UploadLifecycleService.cpp");
            const auto mutation_header = ReadSourceFile("src/services/FileMutationService.hpp");
            const auto mutation_source = ReadSourceFile("src/services/FileMutationService.cpp");
            const auto trash_source = ReadSourceFile("src/services/TrashService.cpp");

            EXPECT_TRUE(Contains(utils_header, "auto ExtractFileExtension("));
            EXPECT_TRUE(Contains(utils_source, "auto ExtractFileExtension("));
            EXPECT_EQ(CountOccurrences(upload_source, "utils::ExtractFileExtension("), 2U);
            EXPECT_EQ(CountOccurrences(mutation_source, "utils::ExtractFileExtension("), 1U);
            EXPECT_FALSE(Contains(upload_source, "auto ExtractExtension("));
            EXPECT_FALSE(Contains(mutation_header, "static auto ExtractExtension("));
            EXPECT_FALSE(Contains(mutation_source, "FileMutationService::ExtractExtension("));
            EXPECT_TRUE(Contains(trash_source, "TrashService::ExtractExtension("));
            EXPECT_TRUE(Contains(trash_source, "paren_pos"));
        }

        TEST(FileExtensionContractTest, PreservesExistingSuffixRules) {
            EXPECT_EQ(utils::ExtractFileExtension("README"), "");
            EXPECT_EQ(utils::ExtractFileExtension("archive."), "");
            EXPECT_EQ(utils::ExtractFileExtension("report.pdf"), "pdf");
            EXPECT_EQ(utils::ExtractFileExtension("archive.tar.gz"), "gz");
            EXPECT_EQ(utils::ExtractFileExtension("PHOTO.JPEG"), "JPEG");
            EXPECT_EQ(utils::ExtractFileExtension(".profile"), "profile");
        }

        TEST(FileServiceUtilsVisibilityContractTest, SnapshotDateConversionStaysInternal) {
            const auto utils_header = ReadSourceFile("src/services/FileServiceUtils.hpp");
            const auto utils_source = ReadSourceFile("src/services/FileServiceUtils.cpp");

            EXPECT_FALSE(Contains(utils_header, "#include <trantor/utils/Date.h>"));
            EXPECT_FALSE(Contains(utils_header, "auto DateToJson("));
            EXPECT_TRUE(Contains(utils_source, "namespace {"));
            EXPECT_EQ(CountOccurrences(utils_source, "auto DateToJson("), 1U);
            EXPECT_EQ(CountOccurrences(utils_source, "DateToJson("), 7U);
            EXPECT_EQ(CountOccurrences(utils_source, "toDbStringLocal()"), 1U);

            const trantor::Date root_created(1'700'000'000'000'000LL);
            const trantor::Date root_updated(1'700'000'001'000'000LL);
            const trantor::Date child_created(1'700'000'002'000'000LL);
            const trantor::Date child_updated(1'700'000'003'000'000LL);
            const trantor::Date file_created(1'700'000'004'000'000LL);
            const trantor::Date file_updated(1'700'000'005'000'000LL);

            drogon_model::disk::Folders root;
            root.setId(1);
            root.setParentId(0);
            root.setName("root");
            root.setPath("/root/");
            root.setDepth(1);
            root.setItemCount(2);
            root.setCreatedAt(root_created);
            root.setUpdatedAt(root_updated);

            drogon_model::disk::Folders child;
            child.setId(2);
            child.setParentId(1);
            child.setName("child");
            child.setPath("/root/child/");
            child.setDepth(2);
            child.setItemCount(0);
            child.setCreatedAt(child_created);
            child.setUpdatedAt(child_updated);

            drogon_model::disk::Files file;
            file.setId(3);
            file.setFolderId(1);
            file.setName("file.txt");
            file.setExtension("txt");
            file.setSize(42);
            file.setMimeType("text/plain");
            file.setPath("/root/file.txt");
            file.setIsFavorite(0);
            file.setDownloadCount(0);
            file.setCreatedAt(file_created);
            file.setUpdatedAt(file_updated);

            utils::FolderDeletePlan plan{
                .root = root,
                .folders = { root, child },
                .files = { file },
                .item_size = 42
            };
            Json::Value snapshot;
            Json::CharReaderBuilder reader;
            std::istringstream input(utils::BuildFolderSnapshot(plan));
            ASSERT_TRUE(Json::parseFromStream(reader, input, &snapshot, nullptr));
            ASSERT_EQ(snapshot["folders"].size(), 1U);
            ASSERT_EQ(snapshot["files"].size(), 1U);
            EXPECT_EQ(snapshot["root"]["created_at"].asString(), root_created.toDbStringLocal());
            EXPECT_EQ(snapshot["root"]["updated_at"].asString(), root_updated.toDbStringLocal());
            EXPECT_EQ(snapshot["folders"][0]["created_at"].asString(), child_created.toDbStringLocal());
            EXPECT_EQ(snapshot["folders"][0]["updated_at"].asString(), child_updated.toDbStringLocal());
            EXPECT_EQ(snapshot["files"][0]["created_at"].asString(), file_created.toDbStringLocal());
            EXPECT_EQ(snapshot["files"][0]["updated_at"].asString(), file_updated.toDbStringLocal());
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
            using NameLockSignature = drogon::Task<void> (FileRepository::*)(
                const drogon::orm::DbClientPtr&,
                uint64_t,
                uint64_t,
                const std::string&
            ) const;
            using NameExistsSignature = drogon::Task<bool> (FileRepository::*)(
                const drogon::orm::DbClientPtr&,
                const std::string&,
                uint64_t,
                uint64_t,
                uint64_t
            ) const;

            static_assert(std::is_same_v<decltype(&FileRepository::FindOwnedFileForUpdate), FindOwnedSignature>);
            static_assert(std::is_same_v<decltype(&FileRepository::FetchOwnedFilesByIdsForUpdate), FetchOwnedSignature>);
            static_assert(std::is_same_v<decltype(&FileRepository::RenameOwnedFile), RenameSignature>);
            static_assert(std::is_same_v<decltype(&FileRepository::AcquireNameLock), NameLockSignature>);
            static_assert(std::is_same_v<decltype(&FileRepository::NameExistsExcluding), NameExistsSignature>);
            static_assert(std::is_default_constructible_v<FileRepository>);
            static_assert(std::is_empty_v<FileRepository>);
        }

        TEST(FileRepositorySignatureContractTest, CarriesNoUnusedDefaultClientState) {
            const auto header = ReadSourceFile("src/services/FileRepository.hpp");
            const auto source = ReadSourceFile("src/services/FileRepository.cpp");
            const auto folder_repository = ReadSourceFile("src/services/FolderRepository.cpp");
            const auto folder_service = ReadSourceFile("src/services/FolderService.cpp");
            const auto file_mutation_service = ReadSourceFile("src/services/FileMutationService.cpp");

            EXPECT_EQ(CountOccurrences(header, "const drogon::orm::DbClientPtr& client,"), 8U);
            EXPECT_EQ(CountOccurrences(source, "const drogon::orm::DbClientPtr& client,"), 8U);
            EXPECT_FALSE(Contains(header, "m_db_client"));
            EXPECT_FALSE(Contains(header, "FileRepository(drogon::orm::DbClientPtr"));
            EXPECT_FALSE(Contains(source, "FileRepository::FileRepository("));
            EXPECT_FALSE(Contains(folder_repository, "FileRepository file_repository(m_db_client)"));
            EXPECT_FALSE(Contains(folder_service, "FileRepository file_repository(m_db_client)"));
            EXPECT_FALSE(Contains(file_mutation_service, "m_file_repository(m_db_client)"));
            EXPECT_EQ(CountOccurrences(folder_repository, "FileRepository file_repository;"), 1U);
            EXPECT_TRUE(Contains(folder_service, "FileRepository file_repository;"));
        }

        TEST(FileRepositorySqlContractTest, OwnershipQueriesStayUserScopedAndBatchSafe) {
            const auto header = ReadSourceFile("src/services/FileRepository.hpp");
            const auto source = ReadSourceFile("src/services/FileRepository.cpp");

            EXPECT_FALSE(Contains(header, "auto FindOwnedFile(\n"));
            EXPECT_FALSE(Contains(source, "auto FileRepository::FindOwnedFile(\n"));
            EXPECT_FALSE(Contains(source, "constexpr auto kSelectOwnedFileSql ="));
            EXPECT_FALSE(Contains(source, "FROM files WHERE id = $1 AND user_id = $2\";"));
            EXPECT_TRUE(Contains(source, "auto FileRepository::FindOwnedFileForUpdate("));
            EXPECT_TRUE(Contains(source, "FROM files WHERE id = $1 AND user_id = $2 FOR UPDATE"));

            EXPECT_TRUE(Contains(source, "auto FileRepository::FetchOwnedFilesByIdsForUpdate("));
            EXPECT_TRUE(Contains(source, "std::sort(sorted_file_ids.begin(), sorted_file_ids.end())"));
            EXPECT_TRUE(Contains(source, "BatchUtils::Chunk(sorted_file_ids, DEFAULT_BATCH_CHUNK_SIZE)"));
            EXPECT_TRUE(Contains(source, "BatchUtils::BuildSafeNumericInClause(chunk)"));
            EXPECT_TRUE(Contains(source, ") AND user_id = $1 ORDER BY id ASC FOR UPDATE"));
            EXPECT_FALSE(Contains(source, "auto FileRepository::FetchOwnedFilesByIds("));
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

        TEST(FileRepositorySqlContractTest, RenameLocksTargetAndNameBeforeConflictCheck) {
            const auto source = ReadSourceFile("src/services/FileRepository.cpp");
            const auto service = ReadSourceFile("src/services/FileMutationService.cpp");

            EXPECT_TRUE(Contains(source, "auto FileRepository::FindOwnedFileForUpdate("));
            EXPECT_TRUE(Contains(source, "FROM files WHERE id = $1 AND user_id = $2 FOR UPDATE"));
            EXPECT_TRUE(Contains(source, "auto FileRepository::AcquireNameLock("));
            EXPECT_TRUE(Contains(source, "file-name:"));
            EXPECT_TRUE(Contains(source, "pg_advisory_xact_lock(hashtextextended($1, 0))"));
            EXPECT_TRUE(Contains(source, "auto FileRepository::NameExistsExcluding("));
            EXPECT_TRUE(Contains(source, "AND id <> $4"));

            const auto rename = service.find("auto FileMutationService::Rename(");
            const auto transaction = service.find("TransactionRunner rename_transaction_runner(", rename);
            const auto target_lock = service.find("FindOwnedFileForUpdate(", transaction);
            const auto name_lock = service.find("AcquireNameLock(", target_lock);
            const auto conflict_check = service.find("NameExistsExcluding(", name_lock);
            const auto folder_location = service.find("ResolveOwnedFolderLocation(", conflict_check);
            const auto update = service.find("RenameOwnedFile(", folder_location);
            ASSERT_NE(rename, std::string::npos);
            ASSERT_NE(transaction, std::string::npos);
            ASSERT_NE(target_lock, std::string::npos);
            ASSERT_NE(name_lock, std::string::npos);
            ASSERT_NE(conflict_check, std::string::npos);
            ASSERT_NE(folder_location, std::string::npos);
            ASSERT_NE(update, std::string::npos);
            EXPECT_LT(transaction, target_lock);
            EXPECT_LT(target_lock, name_lock);
            EXPECT_LT(name_lock, conflict_check);
            EXPECT_LT(conflict_check, folder_location);
            EXPECT_LT(folder_location, update);
            EXPECT_FALSE(Contains(service, "auto FileMutationService::IsFilenameExists("));
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

    } // namespace
} // namespace disk::file
