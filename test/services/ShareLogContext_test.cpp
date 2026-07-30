/**
 * @file ShareLogContext_test.cpp
 * @brief Share request correlation source contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace disk::share {
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

        auto ExtractFrom(const std::string& source, std::string_view begin_marker) -> std::string {
            const auto begin = source.find(begin_marker);
            if (begin == std::string::npos) {
                return {};
            }
            return source.substr(begin);
        }

        auto CallContainsContext(const std::string& source, std::string_view call_marker) -> bool {
            const auto begin = source.find(call_marker);
            if (begin == std::string::npos) {
                return false;
            }
            const auto end = source.find(");", begin);
            return end != std::string::npos &&
                   source.substr(begin, end - begin).find("log_context") != std::string::npos;
        }

        TEST(ShareLogContextContractTest, RequestBoundariesUseExplicitTypedContext) {
            const auto controller_source = ReadSourceFile("src/controllers/ShareController.cpp");
            const auto dto_source = ReadSourceFile("src/dtos/ShareDto.hpp");
            const auto service_header = ReadSourceFile("src/services/ShareService.hpp");
            const auto service_source = ReadSourceFile("src/services/ShareService.cpp");
            const auto audit_header = ReadSourceFile("src/services/ShareAuditService.hpp");
            const auto audit_source = ReadSourceFile("src/services/ShareAuditService.cpp");
            const auto request_service_body =
                ExtractFrom(service_source, "auto ShareService::Create(");

            ASSERT_FALSE(controller_source.empty());
            ASSERT_FALSE(dto_source.empty());
            ASSERT_FALSE(service_header.empty());
            ASSERT_FALSE(request_service_body.empty());
            ASSERT_FALSE(audit_header.empty());
            ASSERT_FALSE(audit_source.empty());

            EXPECT_EQ(
                CountOccurrences(
                    controller_source,
                    "GetRequestLogContext(request, \"share\")"
                ),
                8
            );
            EXPECT_EQ(
                CountOccurrences(
                    controller_source,
                    "GetRequestLogContext(request, \"download\")"
                ),
                2
            );
            for (const auto* call_marker : {
                     "CreateShareRequest::FromRequest(",
                     "ShareListRequest::FromRequest(",
                     "ShareDetailRequest::FromPath(",
                     "UpdateShareRequest::FromRequest(",
                     "CancelShareRequest::FromRequest(",
                     "AccessShareRequest::FromRequest(",
                     "BrowseShareRequest::FromRequest(",
                     "DownloadShareRequest::FromPath(",
                     "SaveShareItemsRequest::FromRequest(",
                     "m_share_service->Create(",
                     "m_share_service->List(",
                     "m_share_service->Detail(",
                     "m_share_service->Update(",
                     "m_share_service->Cancel(",
                     "m_share_service->Access(",
                     "m_share_service->Browse(",
                     "m_share_service->GetDownloadInfo(",
                     "m_share_service->CompleteDownload(",
                     "m_share_service->SaveToDrive(",
                 }) {
                EXPECT_TRUE(CallContainsContext(controller_source, call_marker)) << call_marker;
            }

            EXPECT_EQ(CountOccurrences(dto_source, "disk::utils::LogContext log_context = {}"), 9);
            EXPECT_EQ(
                CountOccurrences(service_header, "disk::utils::LogContext log_context = {}"),
                13
            );
            EXPECT_EQ(
                CountOccurrences(service_header, "disk::utils::LogContext log_context"),
                22
            );
            EXPECT_EQ(
                CountOccurrences(request_service_body, "disk::utils::LogContext log_context"),
                22
            );
            EXPECT_FALSE(Contains(service_header, "auto UpdateTimestamp("));
            EXPECT_FALSE(Contains(service_source, "ShareService::UpdateTimestamp("));
            EXPECT_FALSE(Contains(dto_source, "ShareStatusToString"));

            for (const auto* call_marker : {
                     "co_await ValidateFileOwnership(",
                     "co_await ValidateFolderOwnership(",
                     "co_await GetShareFilesBatch(",
                     "co_await ValidateShareOwnership(",
                     "co_await FindShareByCode(",
                     "co_await HandleFailedShareAccess(",
                     "co_await RecordFailedShareAccess(",
                     "co_await IncrementViewCount(",
                     "co_await GetShareFiles(",
                     "TokenService::GenerateShareToken(",
                     "co_await ValidateShareActive(",
                     "co_await UpdateFileDownloadMetadata(",
                     "co_await IncrementDownloadCount(",
                 }) {
                EXPECT_TRUE(CallContainsContext(request_service_body, call_marker)) << call_marker;
            }

            EXPECT_EQ(
                CountOccurrences(audit_header, "disk::utils::LogContext log_context;"),
                5
            );
            EXPECT_EQ(CountOccurrences(audit_source, "event.log_context"), 5);
            EXPECT_TRUE(Contains(audit_source, "SetLogContext(details, log_context);"));

            for (const auto* body : {
                     &controller_source,
                     &dto_source,
                     &request_service_body,
                     &audit_source,
                 }) {
                EXPECT_FALSE(Contains(*body, "Logger::Debug()"));
                EXPECT_FALSE(Contains(*body, "Logger::Info()"));
                EXPECT_FALSE(Contains(*body, "Logger::Warn()"));
                EXPECT_FALSE(Contains(*body, "Logger::Error()"));
                EXPECT_FALSE(Contains(*body, "<< *request.password"));
                EXPECT_FALSE(Contains(*body, "<< request.password.value()"));
                EXPECT_FALSE(Contains(*body, "<< pwd;"));
                EXPECT_FALSE(Contains(*body, "<< password_hash"));
                EXPECT_FALSE(Contains(*body, "<< *token_result"));
                EXPECT_FALSE(Contains(*body, "<< response.share_token"));
                EXPECT_FALSE(Contains(*body, "<< m_jwt_secret"));
            }
        }

    } // namespace
} // namespace disk::share
