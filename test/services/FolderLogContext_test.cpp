/**
 * @file FolderLogContext_test.cpp
 * @brief Folder request correlation source contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

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

        auto Contains(const std::string& source, std::string_view expected) -> bool {
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

        TEST(FolderLogContextContractTest, ControllerDtoAndServiceUseExplicitRequestContext) {
            const auto controller_source = ReadSourceFile("src/controllers/FolderController.cpp");
            const auto dto_source = ReadSourceFile("src/dtos/FolderDto.hpp");
            const auto service_source = ReadSourceFile("src/services/FolderService.cpp");

            const auto controller_body = ExtractRange(
                controller_source,
                "auto FolderController::CreateFolder(",
                "\n} ///< namespace disk::folder"
            );
            const auto service_body = ExtractRange(
                service_source,
                "auto FolderService::CreateFolder(",
                "\n} // namespace disk::folder"
            );

            ASSERT_FALSE(controller_body.empty());
            ASSERT_FALSE(dto_source.empty());
            ASSERT_FALSE(service_body.empty());
            EXPECT_EQ(
                CountOccurrences(
                    controller_body,
                    "GetRequestLogContext(request, \"folder_query\")"
                ),
                2
            );
            EXPECT_EQ(
                CountOccurrences(
                    controller_body,
                    "GetRequestLogContext(request, \"folder_mutation\")"
                ),
                2
            );
            EXPECT_TRUE(Contains(
                controller_body,
                "CreateFolderRequest::FromRequest(request, log_context)"
            ));
            EXPECT_TRUE(Contains(
                controller_body,
                "FolderTreeRequest::FromRequest(request, log_context)"
            ));
            EXPECT_TRUE(Contains(
                controller_body,
                "RenameFolderRequest::FromPathAndRequest("
            ));
            EXPECT_TRUE(Contains(controller_body, "m_folder_service->CreateFolder("));
            EXPECT_TRUE(Contains(controller_body, "m_folder_service->GetFolderTree("));
            EXPECT_TRUE(Contains(controller_body, "m_folder_service->GetBreadcrumb("));
            EXPECT_TRUE(Contains(controller_body, "m_folder_service->Rename("));

            EXPECT_EQ(CountOccurrences(dto_source, "disk::utils::LogContext log_context = {}"), 3);
            EXPECT_EQ(
                CountOccurrences(dto_source, "::disk::utils::HasForbiddenDriveItemChars("),
                2U
            );
            EXPECT_FALSE(Contains(dto_source, "static const char forbidden_chars[]"));
            EXPECT_EQ(
                CountOccurrences(service_body, "disk::utils::LogContext log_context"),
                6
            );
            EXPECT_TRUE(Contains(service_body, "IsFolderNameExists("));
            EXPECT_TRUE(Contains(service_body, "disk::file::TransactionRunner transaction_runner("));
            EXPECT_TRUE(Contains(service_body, "m_folder_repository.InsertIfNameAvailable("));
            EXPECT_FALSE(Contains(service_body, "FindAndValidateParent("));
            EXPECT_FALSE(Contains(service_body, "IncrementParentItemCount("));
            EXPECT_TRUE(Contains(service_body, "ValidateParentOwnership("));
            EXPECT_TRUE(Contains(
                service_body,
                "Breadcrumb folder not found or no permission"
            ));

            for (const auto* body : { &controller_body, &dto_source, &service_body }) {
                EXPECT_FALSE(Contains(*body, "Logger::Debug()"));
                EXPECT_FALSE(Contains(*body, "Logger::Info()"));
                EXPECT_FALSE(Contains(*body, "Logger::Warn()"));
                EXPECT_FALSE(Contains(*body, "Logger::Error()"));
            }
        }

    } // namespace
} // namespace disk::folder
