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

        TEST(AdminGetUserDetailContractTest, SeparatesMissingRowsFromDatabaseErrors) {
            const auto service_source = ReadSourceFile("src/services/AdminService.cpp");
            const auto detail_body = ExtractRange(
                service_source,
                "auto AdminService::GetUserDetail(",
                "    auto AdminService::ChangeUserStatus("
            );

            ASSERT_FALSE(detail_body.empty());
            EXPECT_EQ(
                CountOccurrences(
                    detail_body,
                    "catch (const drogon::orm::UnexpectedRows&)"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    detail_body,
                    "catch (const drogon::orm::DrogonDbException&)"
                ),
                1U
            );

            const auto missing_catch = detail_body.find(
                "catch (const drogon::orm::UnexpectedRows&)"
            );
            const auto database_catch = detail_body.find(
                "catch (const drogon::orm::DrogonDbException&)"
            );
            EXPECT_NE(missing_catch, std::string::npos);
            EXPECT_NE(database_catch, std::string::npos);
            EXPECT_LT(missing_catch, database_catch);

            EXPECT_EQ(
                CountOccurrences(
                    detail_body,
                    "ErrorInfo(ErrorCode::AdminUserNotFound)"
                ),
                1U
            );
            EXPECT_EQ(CountOccurrences(detail_body, ".what()"), 0U);
            EXPECT_EQ(CountOccurrences(detail_body, "error_msg.find("), 0U);
            EXPECT_EQ(
                CountOccurrences(
                    detail_body,
                    "Logger::Error(log_context)\n                << \"Admin get user detail database error\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    detail_body,
                    "ErrorCode::InternalError,\n                \"Failed to get user detail\""
                ),
                1U
            );
        }

        TEST(AdminChangeUserStatusContractTest, SeparatesMissingRowsFromDatabaseErrors) {
            const auto service_source = ReadSourceFile("src/services/AdminService.cpp");
            const auto status_body = ExtractRange(
                service_source,
                "auto AdminService::ChangeUserStatus(",
                "    auto AdminService::ChangeUserRole("
            );

            ASSERT_FALSE(status_body.empty());
            EXPECT_EQ(
                CountOccurrences(
                    status_body,
                    "catch (const drogon::orm::UnexpectedRows&)"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    status_body,
                    "catch (const drogon::orm::DrogonDbException&)"
                ),
                1U
            );

            const auto missing_catch = status_body.find(
                "catch (const drogon::orm::UnexpectedRows&)"
            );
            const auto database_catch = status_body.find(
                "catch (const drogon::orm::DrogonDbException&)"
            );
            EXPECT_NE(missing_catch, std::string::npos);
            EXPECT_NE(database_catch, std::string::npos);
            EXPECT_LT(missing_catch, database_catch);

            EXPECT_EQ(
                CountOccurrences(
                    status_body,
                    "ErrorInfo(ErrorCode::AdminUserNotFound)"
                ),
                1U
            );
            EXPECT_EQ(CountOccurrences(status_body, ".what()"), 0U);
            EXPECT_EQ(CountOccurrences(status_body, "error_msg.find("), 0U);
            EXPECT_EQ(
                CountOccurrences(
                    status_body,
                    "Logger::Error(log_context)\n                << \"Admin change user status database error\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    status_body,
                    "ErrorCode::InternalError,\n                \"Failed to change user status\""
                ),
                1U
            );

            EXPECT_EQ(
                CountOccurrences(
                    status_body,
                    "user.setStatus(static_cast<int8_t>(status));"
                ),
                1U
            );
            EXPECT_EQ(CountOccurrences(status_body, "user.setLoginAttempts(0);"), 1U);
            EXPECT_EQ(CountOccurrences(status_body, "user.setLockedUntilToNull();"), 1U);
            EXPECT_EQ(CountOccurrences(status_body, "co_await mapper.update(user);"), 1U);
            EXPECT_EQ(
                CountOccurrences(status_body, "\"admin.user.status_change\""),
                1U
            );
        }

        TEST(AdminListSharesLogContractTest, DatabaseExceptionUsesFixedSummary) {
            const auto service_source = ReadSourceFile("src/services/AdminService.cpp");
            const auto list_body = ExtractRange(
                service_source,
                "auto AdminService::ListShares(",
                "    auto AdminService::GetShareDetail("
            );

            ASSERT_FALSE(list_body.empty());
            EXPECT_EQ(CountOccurrences(list_body, ".what()"), 0U);
            EXPECT_EQ(
                CountOccurrences(
                    list_body,
                    "Logger::Error(log_context) << \"Admin list shares database error\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    list_body,
                    "ErrorInfo(\n                ErrorCode::InternalError,\n                \"Failed to list shares\""
                ),
                1U
            );
            EXPECT_EQ(CountOccurrences(list_body, "execSqlCoro("), 5U);
            EXPECT_TRUE(Contains(list_body, "AND u.username LIKE $"));
            EXPECT_TRUE(Contains(list_body, "ORDER BY s.created_at DESC LIMIT $"));
            EXPECT_EQ(
                CountOccurrences(list_body, "static_cast<int64_t>("),
                2U
            );
            EXPECT_EQ(
                CountOccurrences(list_body, "row[\"password_set\"].as<int>()"),
                0U
            );
            EXPECT_EQ(
                CountOccurrences(list_body, "row[\"password_set\"].as<bool>()"),
                1U
            );
        }

        TEST(AdminShareExternalIdentifierContractTest, PathsAndResponsesUseShareCode) {
            const auto controller_header =
                ReadSourceFile("src/controllers/AdminController.hpp");
            const auto controller_source =
                ReadSourceFile("src/controllers/AdminController.cpp");
            const auto dto_source = ReadSourceFile("src/dtos/AdminDto.hpp");
            const auto service_header = ReadSourceFile("src/services/AdminService.hpp");
            const auto service_source = ReadSourceFile("src/services/AdminService.cpp");

            const auto response_dto = ExtractRange(
                dto_source,
                "struct ShareDetailResponse",
                "    struct ShareListResponse"
            );
            const auto controller_detail = ExtractRange(
                controller_source,
                "auto AdminController::GetShareDetail(",
                "    auto AdminController::ForceCancelShare("
            );
            const auto controller_cancel = ExtractRange(
                controller_source,
                "auto AdminController::ForceCancelShare(",
                "    auto AdminController::GetOverviewStats("
            );
            const auto service_detail = ExtractRange(
                service_source,
                "auto AdminService::GetShareDetail(",
                "    auto AdminService::ForceCancelShare("
            );
            const auto service_cancel = ExtractRange(
                service_source,
                "auto AdminService::ForceCancelShare(",
                "    auto AdminService::GetOverviewStats("
            );

            ASSERT_FALSE(response_dto.empty());
            ASSERT_FALSE(controller_detail.empty());
            ASSERT_FALSE(controller_cancel.empty());
            ASSERT_FALSE(service_detail.empty());
            ASSERT_FALSE(service_cancel.empty());

            EXPECT_EQ(CountOccurrences(response_dto, "std::string share_id;"), 1U);
            EXPECT_EQ(
                CountOccurrences(response_dto, "SetField(json, \"share_id\", share_id);"),
                1U
            );
            EXPECT_FALSE(Contains(response_dto, "uint64_t id;"));
            EXPECT_FALSE(Contains(response_dto, "share_code"));

            EXPECT_EQ(
                CountOccurrences(controller_header, "/api/admin/shares/{share_id}"),
                2U
            );
            EXPECT_EQ(CountOccurrences(controller_detail, "std::stoull("), 0U);
            EXPECT_EQ(CountOccurrences(controller_cancel, "std::stoull("), 0U);
            EXPECT_TRUE(Contains(
                controller_detail,
                "co_return Response::Success(result->ToJson());"
            ));
            EXPECT_FALSE(Contains(controller_detail, "data[\"share\"]"));

            EXPECT_EQ(
                CountOccurrences(
                    service_header,
                    "const std::string& share_id,\n            uint64_t operator_id"
                ),
                2U
            );
            EXPECT_EQ(CountOccurrences(service_detail, "s.share_code = $1"), 2U);
            EXPECT_EQ(CountOccurrences(service_detail, ".what()"), 0U);
            EXPECT_TRUE(Contains(
                service_detail,
                "row[\"password_set\"].as<bool>()"
            ));
            EXPECT_TRUE(Contains(
                service_detail,
                "Logger::Error(log_context) << \"Admin get share detail database error\";"
            ));
            EXPECT_TRUE(Contains(service_cancel, "WHERE share_code = $1"));
            EXPECT_TRUE(Contains(service_cancel, "WHERE id = $1"));
            EXPECT_EQ(CountOccurrences(service_cancel, ".what()"), 0U);
            EXPECT_TRUE(Contains(
                service_cancel,
                "Logger::Error(log_context) << \"Admin force cancel share database error\";"
            ));
            EXPECT_TRUE(Contains(service_source, "share.share_id = row[\"share_code\"]"));
            EXPECT_FALSE(Contains(service_source, "share.id = row[\"id\"]"));
            EXPECT_FALSE(Contains(service_source, "share.share_code = row[\"share_code\"]"));
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
