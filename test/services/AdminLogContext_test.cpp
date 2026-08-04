/**
 * @file AdminLogContext_test.cpp
 * @brief Core administration request correlation source contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace disk::admin {
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

        auto AllCallsContainContext(
            const std::string& source,
            std::string_view call_marker
        ) -> bool {
            size_t position = 0;
            size_t count = 0;
            while ((position = source.find(call_marker, position)) != std::string::npos) {
                const auto end = source.find(");", position);
                if (end == std::string::npos ||
                    source.substr(position, end - position).find("log_context") ==
                        std::string::npos) {
                    return false;
                }
                ++count;
                position = end + 2;
            }
            return count > 0;
        }

        TEST(AdminLogContextContractTest, DomainDetailsShareRequestCorrelationFields) {
            const auto log_header = ReadSourceFile("src/utils/LogHelper.hpp");
            const auto log_source = ReadSourceFile("src/utils/LogHelper.cpp");
            const auto admin_source = ReadSourceFile("src/services/AdminService.cpp");
            const auto share_source = ReadSourceFile("src/services/ShareAuditService.cpp");
            const auto job_source =
                ReadSourceFile("src/services/StorageJobAdminService.cpp");
            const auto recovery_source =
                ReadSourceFile("src/services/StorageRecoveryAdminService.cpp");
            const auto download_source =
                ReadSourceFile("src/services/DownloadIntegrityService.cpp");

            EXPECT_EQ(
                CountOccurrences(log_header, "auto SetRequestCorrelationFields("),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(log_source, "auto SetRequestCorrelationFields("),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(log_source, "SetNullableString(details, \"request_id\""),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(log_source, "SetNullableString(details, \"operation\""),
                1U
            );

            for (const auto* source : {
                     &admin_source,
                     &share_source,
                     &job_source,
                     &recovery_source,
                     &download_source,
                 }) {
                EXPECT_EQ(CountOccurrences(*source, "#include \"utils/LogHelper.hpp\""), 1U);
            }
            EXPECT_EQ(
                CountOccurrences(admin_source, "disk::utils::SetRequestCorrelationFields("),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(share_source, "disk::utils::SetRequestCorrelationFields("),
                5U
            );
            EXPECT_EQ(
                CountOccurrences(job_source, "disk::utils::SetRequestCorrelationFields("),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(recovery_source, "disk::utils::SetRequestCorrelationFields("),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(download_source, "disk::utils::SetRequestCorrelationFields("),
                4U
            );
            EXPECT_FALSE(Contains(admin_source, "auto SetLogContext("));
            EXPECT_FALSE(Contains(share_source, "auto SetLogContext("));
            EXPECT_FALSE(Contains(job_source, "auto SetLogContext("));
            EXPECT_FALSE(Contains(recovery_source, "auto SetLogContext("));
            EXPECT_FALSE(Contains(download_source, "auto AddCorrelationDetails("));
        }

        TEST(AdminListUsersLogContractTest, DatabaseExceptionUsesFixedSummary) {
            const auto service_source = ReadSourceFile("src/services/AdminService.cpp");
            const auto list_body = ExtractRange(
                service_source,
                "auto AdminService::ListUsers(",
                "    auto AdminService::GetUserDetail("
            );

            ASSERT_FALSE(list_body.empty());
            EXPECT_EQ(CountOccurrences(list_body, ".what()"), 0U);
            EXPECT_EQ(
                CountOccurrences(
                    list_body,
                    "Logger::Error(log_context) << \"Admin list users database error\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    list_body,
                    "ErrorInfo(\n                ErrorCode::InternalError,\n                \"Failed to list users\""
                ),
                1U
            );
            EXPECT_EQ(CountOccurrences(list_body, "execSqlCoro("), 2U);
            EXPECT_TRUE(Contains(list_body, "ORDER BY created_at DESC LIMIT $1 OFFSET $2"));
            EXPECT_EQ(
                CountOccurrences(list_body, "static_cast<int64_t>("),
                2U
            );
        }

        TEST(AdminLogContextContractTest, CoreBoundariesUseExplicitTypedContext) {
            const auto controller_source =
                ReadSourceFile("src/controllers/AdminController.cpp");
            const auto dto_source = ReadSourceFile("src/dtos/AdminDto.hpp");
            const auto service_header = ReadSourceFile("src/services/AdminService.hpp");
            const auto service_source = ReadSourceFile("src/services/AdminService.cpp");
            const auto request_service_body = ExtractRange(
                service_source,
                "auto AdminService::ListUsers(",
                "    auto AdminService::LogOperation("
            );

            ASSERT_FALSE(controller_source.empty());
            ASSERT_FALSE(dto_source.empty());
            ASSERT_FALSE(service_header.empty());
            ASSERT_FALSE(request_service_body.empty());

            EXPECT_EQ(
                CountOccurrences(
                    controller_source,
                    "GetRequestLogContext(request, \"admin\")"
                ),
                13
            );
            EXPECT_EQ(
                CountOccurrences(
                    controller_source,
                    "GetRequestLogContext(request, \"cleanup\")"
                ),
                1
            );

            for (const auto* call_marker : {
                     "ListUsersRequest::FromRequest(",
                     "ChangeStatusRequest::FromRequest(",
                     "ChangeRoleRequest::FromRequest(",
                     "ChangeAvailableSpaceRequest::FromRequest(",
                     "ListSharesRequest::FromRequest(",
                     "AdminLogListRequest::FromRequest(",
                     "service->ListUsers(",
                     "service->GetUserDetail(",
                     "service->ChangeUserStatus(",
                     "service->ChangeUserRole(",
                     "service->ChangeUserAvailableSpace(",
                     "service->SoftDeleteUser(",
                     "service->GetGlobalStorageStats(",
                     "service->ListShares(",
                     "service->GetShareDetail(",
                     "service->ForceCancelShare(",
                     "service->GetOverviewStats(",
                     "service->GetSystemStatus(",
                     "service->GetAdminLogs(",
                 }) {
                EXPECT_TRUE(AllCallsContainContext(controller_source, call_marker))
                    << call_marker;
            }

            EXPECT_EQ(
                CountOccurrences(dto_source, "disk::utils::LogContext log_context = {}"),
                6
            );
            EXPECT_EQ(
                CountOccurrences(
                    service_header,
                    "disk::utils::LogContext log_context = {}"
                ),
                13
            );
            EXPECT_EQ(
                CountOccurrences(
                    request_service_body,
                    "disk::utils::LogContext log_context"
                ),
                13
            );
            EXPECT_EQ(
                CountOccurrences(request_service_body, "co_await LogOperation("),
                8
            );
            EXPECT_TRUE(AllCallsContainContext(request_service_body, "co_await LogOperation("));

            EXPECT_TRUE(Contains(
                service_source,
                "disk::utils::SetRequestCorrelationFields(details, log_context);"
            ));
            EXPECT_TRUE(Contains(service_source, "SerializeDetails(details)"));
            EXPECT_FALSE(Contains(service_source, "std::format("));
            EXPECT_FALSE(Contains(request_service_body, "LogOperation(0"));
            EXPECT_TRUE(Contains(request_service_body, "admin.user.available_space_set"));
            EXPECT_FALSE(Contains(request_service_body, "admin.user.available_space_change"));
            EXPECT_EQ(
                CountOccurrences(
                    request_service_body,
                    "UPDATE shares AS s SET status = 0, updated_at = NOW()"
                ),
                2
            );
            EXPECT_FALSE(Contains(request_service_body, "SET s.status"));

            for (const auto* body : {
                     &controller_source,
                     &dto_source,
                     &request_service_body,
                 }) {
                EXPECT_FALSE(Contains(*body, "Logger::Debug()"));
                EXPECT_FALSE(Contains(*body, "Logger::Info()"));
                EXPECT_FALSE(Contains(*body, "Logger::Warn()"));
                EXPECT_FALSE(Contains(*body, "Logger::Error()"));
            }
        }

    } // namespace
} // namespace disk::admin
