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

        TEST(AdminListUsersControllerValueLogContractTest, ListUsesFixedSummaries) {
            const auto controller_source =
                ReadSourceFile("src/controllers/AdminController.cpp");
            const auto dto_source = ReadSourceFile("src/dtos/AdminDto.hpp");
            const auto list_controller = ExtractRange(
                controller_source,
                "auto AdminController::ListUsers(",
                "    auto AdminController::GetUserDetail("
            );
            const auto list_response = ExtractRange(
                dto_source,
                "struct UserListResponse",
                "    struct StorageStatsResponse"
            );

            ASSERT_FALSE(list_controller.empty());
            ASSERT_FALSE(list_response.empty());
            for (const auto* fixed_log : {
                     "Logger::Info(log_context) << \"Admin list users request\";",
                     "Logger::Warn(log_context) << \"List users request validation failed\";",
                     "Logger::Error(log_context) << \"Failed to list users\";",
                     "Logger::Info(log_context) << \"Admin list users successful\";",
                 }) {
                EXPECT_EQ(CountOccurrences(list_controller, fixed_log), 1U) << fixed_log;
            }
            for (const auto* raw_boundary_log : {
                     "<< \"Admin list users request: \" << request->getPeerAddr().toIpPort()",
                     "<< \"List users request validation failed: \" << parse_result.error().message",
                     "<< \"Failed to list users: \" << result.error().message",
                 }) {
                EXPECT_EQ(CountOccurrences(list_controller, raw_boundary_log), 0U)
                    << raw_boundary_log;
            }

            EXPECT_EQ(CountOccurrences(list_controller, "Logger::Info(log_context)"), 2U);
            EXPECT_EQ(CountOccurrences(list_controller, "Logger::Warn(log_context)"), 1U);
            EXPECT_EQ(CountOccurrences(list_controller, "Logger::Debug(log_context)"), 0U);
            EXPECT_EQ(CountOccurrences(list_controller, "Logger::Error(log_context)"), 1U);
            for (const auto* preserved_controller_step : {
                     "GetRequestLogContext(request, \"admin\")",
                     "admin::ListUsersRequest::FromRequest(request, log_context)",
                     "if (!parse_result)",
                     "co_return Response::Error(parse_result.error());",
                     "services::AdminService::GetInstance()",
                     "service->ListUsers(*parse_result, log_context)",
                     "if (!result)",
                     "co_return Response::Error(result.error());",
                     "co_return Response::Success(result->ToJson());",
                 }) {
                EXPECT_EQ(CountOccurrences(list_controller, preserved_controller_step), 1U)
                    << preserved_controller_step;
            }
            for (const auto* response_mapping : {
                     "SetArray(json, \"items\", items);",
                     "SetField(json, \"pagination\", pagination);",
                     "return json;",
                 }) {
                EXPECT_EQ(CountOccurrences(list_response, response_mapping), 1U)
                    << response_mapping;
            }
        }

        TEST(AdminGetUserDetailControllerValueLogContractTest, DetailUsesFixedSummaries) {
            const auto controller_source =
                ReadSourceFile("src/controllers/AdminController.cpp");
            const auto controller_header =
                ReadSourceFile("src/controllers/AdminController.hpp");
            const auto dto_source = ReadSourceFile("src/dtos/AdminDto.hpp");
            const auto detail_controller = ExtractRange(
                controller_source,
                "auto AdminController::GetUserDetail(",
                "    auto AdminController::ChangeUserStatus("
            );
            const auto detail_route = ExtractRange(
                controller_header,
                "ADD_METHOD_TO(\n            AdminController::GetUserDetail,",
                "        ADD_METHOD_TO(\n            AdminController::ChangeUserStatus,"
            );
            const auto detail_response = ExtractRange(
                dto_source,
                "struct UserDetailResponse",
                "    struct UserListResponse"
            );

            ASSERT_FALSE(detail_controller.empty());
            ASSERT_FALSE(detail_route.empty());
            ASSERT_FALSE(detail_response.empty());
            for (const auto* fixed_log : {
                     "Logger::Info(log_context) << \"Admin get user detail request\";",
                     "Logger::Error(log_context) << \"Failed to get user detail\";",
                     "Logger::Info(log_context) << \"Admin get user detail successful\";",
                 }) {
                EXPECT_EQ(CountOccurrences(detail_controller, fixed_log), 1U) << fixed_log;
            }
            for (const auto* raw_boundary_log : {
                     "<< \"Admin get user detail request: \" << request->getPeerAddr().toIpPort()",
                     "<< \"Failed to get user detail: \" << result.error().message",
                     "<< \"Admin get user detail successful: user_id=\" << user_id",
                 }) {
                EXPECT_EQ(CountOccurrences(detail_controller, raw_boundary_log), 0U)
                    << raw_boundary_log;
            }

            EXPECT_EQ(CountOccurrences(detail_controller, "Logger::Info(log_context)"), 2U);
            EXPECT_EQ(CountOccurrences(detail_controller, "Logger::Warn(log_context)"), 0U);
            EXPECT_EQ(CountOccurrences(detail_controller, "Logger::Debug(log_context)"), 0U);
            EXPECT_EQ(CountOccurrences(detail_controller, "Logger::Error(log_context)"), 1U);
            EXPECT_EQ(CountOccurrences(detail_controller, "ErrorCode::ValidationFailed"), 2U);
            for (const auto* preserved_controller_step : {
                     "GetRequestLogContext(request, \"admin\")",
                     "if (id.empty())",
                     "\"Missing required parameter: id\"",
                     "user_id = std::stoull(id);",
                     "catch (const std::exception&)",
                     "\"Invalid user id format\"",
                     "services::AdminService::GetInstance()",
                     "service->GetUserDetail(user_id, log_context)",
                     "if (!result)",
                     "co_return Response::Error(result.error());",
                     "data[\"user\"] = result->ToJson();",
                     "co_return Response::Success(data);",
                 }) {
                EXPECT_EQ(CountOccurrences(detail_controller, preserved_controller_step), 1U)
                    << preserved_controller_step;
            }

            EXPECT_EQ(CountOccurrences(detail_route, "\"/api/admin/users/{id}\""), 1U);
            EXPECT_EQ(CountOccurrences(detail_route, "drogon::Get"), 1U);
            EXPECT_EQ(
                CountOccurrences(detail_route, "\"disk::filters::AdminAuthFilter\""),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(detail_route, "\"disk::filters::AdminRateLimitFilter\""),
                1U
            );
            EXPECT_LT(
                detail_route.find("\"disk::filters::AdminAuthFilter\""),
                detail_route.find("\"disk::filters::AdminRateLimitFilter\"")
            );

            for (const auto* response_field : {
                     "id",
                     "username",
                     "email",
                     "nickname",
                     "avatar",
                     "role",
                     "status",
                     "storage_quota",
                     "storage_used",
                     "storage_reserved",
                     "created_at",
                     "last_login_at",
                 }) {
                const auto mapping =
                    std::string("SetField(json, \"") + response_field + "\", " +
                    response_field + ");";
                EXPECT_EQ(CountOccurrences(detail_response, mapping), 1U) << mapping;
            }
        }

        TEST(AdminChangeUserStatusControllerValueLogContractTest, StatusUsesFixedSummaries) {
            const auto controller_source =
                ReadSourceFile("src/controllers/AdminController.cpp");
            const auto controller_header =
                ReadSourceFile("src/controllers/AdminController.hpp");
            const auto dto_source = ReadSourceFile("src/dtos/AdminDto.hpp");
            const auto status_controller = ExtractRange(
                controller_source,
                "auto AdminController::ChangeUserStatus(",
                "    auto AdminController::ChangeUserRole("
            );
            const auto status_route = ExtractRange(
                controller_header,
                "ADD_METHOD_TO(\n            AdminController::ChangeUserStatus,",
                "        ADD_METHOD_TO(\n            AdminController::ChangeUserRole,"
            );
            const auto status_request = ExtractRange(
                dto_source,
                "struct ChangeStatusRequest",
                "    struct ChangeRoleRequest"
            );

            ASSERT_FALSE(status_controller.empty());
            ASSERT_FALSE(status_route.empty());
            ASSERT_FALSE(status_request.empty());
            for (const auto* fixed_log : {
                     "Logger::Info(log_context) << \"Admin change user status request\";",
                     "Logger::Warn(log_context) << \"Change status request validation failed\";",
                     "Logger::Error(log_context) << \"Failed to change user status\";",
                     "Logger::Info(log_context) << \"Admin change user status successful\";",
                 }) {
                EXPECT_EQ(CountOccurrences(status_controller, fixed_log), 1U) << fixed_log;
            }
            for (const auto* raw_boundary_log : {
                     "<< \"Admin change user status request: \" << request->getPeerAddr().toIpPort()",
                     "<< \"Change status request validation failed: \" << parse_result.error().message",
                     "<< \"Failed to change user status: \" << result.error().message",
                     "<< \"Admin change user status successful: target_id=\" << target_id",
                 }) {
                EXPECT_EQ(CountOccurrences(status_controller, raw_boundary_log), 0U)
                    << raw_boundary_log;
            }

            EXPECT_EQ(CountOccurrences(status_controller, "Logger::Info(log_context)"), 2U);
            EXPECT_EQ(CountOccurrences(status_controller, "Logger::Warn(log_context)"), 1U);
            EXPECT_EQ(CountOccurrences(status_controller, "Logger::Debug(log_context)"), 0U);
            EXPECT_EQ(CountOccurrences(status_controller, "Logger::Error(log_context)"), 1U);
            EXPECT_EQ(CountOccurrences(status_controller, "ErrorCode::ValidationFailed"), 2U);
            for (const auto* preserved_controller_step : {
                     "GetRequestLogContext(request, \"admin\")",
                     "request->attributes()->get<uint64_t>(\"user_id\")",
                     "if (id.empty())",
                     "\"Missing required parameter: id\"",
                     "target_id = std::stoull(id);",
                     "catch (const std::exception&)",
                     "\"Invalid user id format\"",
                     "admin::ChangeStatusRequest::FromRequest(request, log_context)",
                     "if (!parse_result)",
                     "co_return Response::Error(parse_result.error());",
                     "services::AdminService::GetInstance()",
                     "service->ChangeUserStatus(\n            target_id,\n            parse_result->status,\n            operator_id,\n            log_context\n        )",
                     "if (!result)",
                     "co_return Response::Error(result.error());",
                     "co_return Response::Success();",
                 }) {
                EXPECT_EQ(CountOccurrences(status_controller, preserved_controller_step), 1U)
                    << preserved_controller_step;
            }

            EXPECT_EQ(
                CountOccurrences(status_route, "\"/api/admin/users/{id}/status\""),
                1U
            );
            EXPECT_EQ(CountOccurrences(status_route, "drogon::Put"), 1U);
            EXPECT_EQ(
                CountOccurrences(status_route, "\"disk::filters::AdminAuthFilter\""),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(status_route, "\"disk::filters::AdminRateLimitFilter\""),
                1U
            );
            EXPECT_LT(
                status_route.find("\"disk::filters::AdminAuthFilter\""),
                status_route.find("\"disk::filters::AdminRateLimitFilter\"")
            );

            for (const auto* preserved_dto_step : {
                     "RequireInt(json, \"status\")",
                     "if (*status_result < 0 || *status_result > 2)",
                     "ErrorInfo(ErrorCode::AdminInvalidStatus, \"Invalid status value\")",
                     "request.status = *status_result;",
                     "return request;",
                 }) {
                EXPECT_EQ(CountOccurrences(status_request, preserved_dto_step), 1U)
                    << preserved_dto_step;
            }
        }

        TEST(AdminChangeUserRoleControllerValueLogContractTest, RoleUsesFixedSummaries) {
            const auto controller_source =
                ReadSourceFile("src/controllers/AdminController.cpp");
            const auto controller_header =
                ReadSourceFile("src/controllers/AdminController.hpp");
            const auto dto_source = ReadSourceFile("src/dtos/AdminDto.hpp");
            const auto role_controller = ExtractRange(
                controller_source,
                "auto AdminController::ChangeUserRole(",
                "    auto AdminController::ChangeUserAvailableSpace("
            );
            const auto role_route = ExtractRange(
                controller_header,
                "ADD_METHOD_TO(\n            AdminController::ChangeUserRole,",
                "        ADD_METHOD_TO(\n            AdminController::ChangeUserAvailableSpace,"
            );
            const auto role_request = ExtractRange(
                dto_source,
                "struct ChangeRoleRequest",
                "    struct ChangeAvailableSpaceRequest"
            );

            ASSERT_FALSE(role_controller.empty());
            ASSERT_FALSE(role_route.empty());
            ASSERT_FALSE(role_request.empty());
            for (const auto* fixed_log : {
                     "Logger::Info(log_context) << \"Admin change user role request\";",
                     "Logger::Warn(log_context) << \"Change role request validation failed\";",
                     "Logger::Error(log_context) << \"Failed to change user role\";",
                     "Logger::Info(log_context) << \"Admin change user role successful\";",
                 }) {
                EXPECT_EQ(CountOccurrences(role_controller, fixed_log), 1U) << fixed_log;
            }
            for (const auto* raw_boundary_log : {
                     "<< \"Admin change user role request: \" << request->getPeerAddr().toIpPort()",
                     "<< \"Change role request validation failed: \" << parse_result.error().message",
                     "<< \"Failed to change user role: \" << result.error().message",
                     "<< \"Admin change user role successful: target_id=\" << target_id",
                 }) {
                EXPECT_EQ(CountOccurrences(role_controller, raw_boundary_log), 0U)
                    << raw_boundary_log;
            }

            EXPECT_EQ(CountOccurrences(role_controller, "Logger::Info(log_context)"), 2U);
            EXPECT_EQ(CountOccurrences(role_controller, "Logger::Warn(log_context)"), 1U);
            EXPECT_EQ(CountOccurrences(role_controller, "Logger::Debug(log_context)"), 0U);
            EXPECT_EQ(CountOccurrences(role_controller, "Logger::Error(log_context)"), 1U);
            EXPECT_EQ(CountOccurrences(role_controller, "ErrorCode::ValidationFailed"), 2U);
            for (const auto* preserved_controller_step : {
                     "GetRequestLogContext(request, \"admin\")",
                     "request->attributes()->get<uint64_t>(\"user_id\")",
                     "if (id.empty())",
                     "\"Missing required parameter: id\"",
                     "target_id = std::stoull(id);",
                     "catch (const std::exception&)",
                     "\"Invalid user id format\"",
                     "admin::ChangeRoleRequest::FromRequest(request, log_context)",
                     "if (!parse_result)",
                     "co_return Response::Error(parse_result.error());",
                     "services::AdminService::GetInstance()",
                     "service->ChangeUserRole(\n            target_id,\n            parse_result->role,\n            operator_id,\n            log_context\n        )",
                     "if (!result)",
                     "co_return Response::Error(result.error());",
                     "co_return Response::Success();",
                 }) {
                EXPECT_EQ(CountOccurrences(role_controller, preserved_controller_step), 1U)
                    << preserved_controller_step;
            }

            EXPECT_EQ(
                CountOccurrences(role_route, "\"/api/admin/users/{id}/role\""),
                1U
            );
            EXPECT_EQ(CountOccurrences(role_route, "drogon::Put"), 1U);
            EXPECT_EQ(
                CountOccurrences(role_route, "\"disk::filters::AdminAuthFilter\""),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(role_route, "\"disk::filters::AdminRateLimitFilter\""),
                1U
            );
            EXPECT_LT(
                role_route.find("\"disk::filters::AdminAuthFilter\""),
                role_route.find("\"disk::filters::AdminRateLimitFilter\"")
            );

            for (const auto* preserved_dto_step : {
                     "RequireInt(json, \"role\")",
                     "if (*role_result < 0 || *role_result > 1)",
                     "ErrorInfo(ErrorCode::AdminInvalidRole, \"Invalid role value\")",
                     "request.role = *role_result;",
                     "return request;",
                 }) {
                EXPECT_EQ(CountOccurrences(role_request, preserved_dto_step), 1U)
                    << preserved_dto_step;
            }
        }

        TEST(
            AdminChangeUserAvailableSpaceControllerValueLogContractTest,
            AvailableSpaceUsesFixedSummaries
        ) {
            const auto controller_source =
                ReadSourceFile("src/controllers/AdminController.cpp");
            const auto controller_header =
                ReadSourceFile("src/controllers/AdminController.hpp");
            const auto dto_source = ReadSourceFile("src/dtos/AdminDto.hpp");
            const auto available_space_controller = ExtractRange(
                controller_source,
                "auto AdminController::ChangeUserAvailableSpace(",
                "    auto AdminController::SoftDeleteUser("
            );
            const auto available_space_route = ExtractRange(
                controller_header,
                "ADD_METHOD_TO(\n            AdminController::ChangeUserAvailableSpace,",
                "        ADD_METHOD_TO(\n            AdminController::SoftDeleteUser,"
            );
            const auto available_space_request = ExtractRange(
                dto_source,
                "struct ChangeAvailableSpaceRequest",
                "    struct ListSharesRequest"
            );
            const auto user_detail_response = ExtractRange(
                dto_source,
                "struct UserDetailResponse",
                "    struct UserListResponse"
            );

            ASSERT_FALSE(available_space_controller.empty());
            ASSERT_FALSE(available_space_route.empty());
            ASSERT_FALSE(available_space_request.empty());
            ASSERT_FALSE(user_detail_response.empty());
            for (const auto* fixed_log : {
                     "Logger::Info(log_context) << \"Admin change user available space request\";",
                     "Logger::Warn(log_context) << \"Change available space request validation failed\";",
                     "Logger::Error(log_context) << \"Failed to change user available space\";",
                     "Logger::Info(log_context) << \"Admin change user available space successful\";",
                 }) {
                EXPECT_EQ(CountOccurrences(available_space_controller, fixed_log), 1U)
                    << fixed_log;
            }
            for (const auto* raw_boundary_log : {
                     "<< \"Admin change user available space request: \"\n            << request->getPeerAddr().toIpPort()",
                     "<< \"Change available space request validation failed: \"\n                << parse_result.error().message",
                     "<< \"Failed to change user available space: \" << result.error().message",
                     "<< \"Admin change user available space successful: target_id=\" << target_id",
                 }) {
                EXPECT_EQ(
                    CountOccurrences(available_space_controller, raw_boundary_log),
                    0U
                ) << raw_boundary_log;
            }

            EXPECT_EQ(
                CountOccurrences(available_space_controller, "Logger::Info(log_context)"),
                2U
            );
            EXPECT_EQ(
                CountOccurrences(available_space_controller, "Logger::Warn(log_context)"),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(available_space_controller, "Logger::Debug(log_context)"),
                0U
            );
            EXPECT_EQ(
                CountOccurrences(available_space_controller, "Logger::Error(log_context)"),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(available_space_controller, "ErrorCode::ValidationFailed"),
                2U
            );
            for (const auto* preserved_controller_step : {
                     "GetRequestLogContext(request, \"admin\")",
                     "request->attributes()->get<uint64_t>(\"user_id\")",
                     "if (id.empty())",
                     "\"Missing required parameter: id\"",
                     "target_id = std::stoull(id);",
                     "catch (const std::exception&)",
                     "\"Invalid user id format\"",
                     "admin::ChangeAvailableSpaceRequest::FromRequest(request, log_context)",
                     "if (!parse_result)",
                     "co_return Response::Error(parse_result.error());",
                     "services::AdminService::GetInstance()",
                     "service->ChangeUserAvailableSpace(\n            target_id,\n            parse_result->available_space_g,\n            operator_id,\n            log_context\n        )",
                     "if (!result)",
                     "co_return Response::Error(result.error());",
                     "data[\"user\"] = result->ToJson();",
                     "co_return Response::Success(data);",
                 }) {
                EXPECT_EQ(
                    CountOccurrences(available_space_controller, preserved_controller_step),
                    1U
                ) << preserved_controller_step;
            }

            EXPECT_EQ(
                CountOccurrences(
                    available_space_route,
                    "\"/api/admin/users/{id}/available-space\""
                ),
                1U
            );
            EXPECT_EQ(CountOccurrences(available_space_route, "drogon::Put"), 1U);
            EXPECT_EQ(
                CountOccurrences(
                    available_space_route,
                    "\"disk::filters::AdminAuthFilter\""
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    available_space_route,
                    "\"disk::filters::AdminRateLimitFilter\""
                ),
                1U
            );
            EXPECT_LT(
                available_space_route.find("\"disk::filters::AdminAuthFilter\""),
                available_space_route.find("\"disk::filters::AdminRateLimitFilter\"")
            );

            for (const auto* preserved_dto_step : {
                     "static constexpr uint64_t BytesPerG = 1024ULL * 1024ULL * 1024ULL;",
                     "RequireUInt64(json, \"available_space_g\")",
                     "if (*space_result > std::numeric_limits<uint64_t>::max() / BytesPerG)",
                     "ErrorCode::ValidationFailed",
                     "\"Parameter 'available_space_g' is too large\"",
                     "request.available_space_g = *space_result;",
                     "return request;",
                 }) {
                EXPECT_EQ(
                    CountOccurrences(available_space_request, preserved_dto_step),
                    1U
                ) << preserved_dto_step;
            }
            for (const auto* response_field : {
                     "id",
                     "username",
                     "email",
                     "nickname",
                     "avatar",
                     "role",
                     "status",
                     "storage_quota",
                     "storage_used",
                     "storage_reserved",
                     "created_at",
                     "last_login_at",
                 }) {
                const auto mapping =
                    std::string("SetField(json, \"") + response_field + "\", " +
                    response_field + ");";
                EXPECT_EQ(CountOccurrences(user_detail_response, mapping), 1U) << mapping;
            }
        }

        TEST(AdminSoftDeleteUserControllerValueLogContractTest, DeleteUsesFixedSummaries) {
            const auto controller_source =
                ReadSourceFile("src/controllers/AdminController.cpp");
            const auto controller_header =
                ReadSourceFile("src/controllers/AdminController.hpp");
            const auto delete_controller = ExtractRange(
                controller_source,
                "auto AdminController::SoftDeleteUser(",
                "    auto AdminController::GetGlobalStorageStats("
            );
            const auto delete_route = ExtractRange(
                controller_header,
                "ADD_METHOD_TO(\n            AdminController::SoftDeleteUser,",
                "        ADD_METHOD_TO(\n            AdminController::GetGlobalStorageStats,"
            );

            ASSERT_FALSE(delete_controller.empty());
            ASSERT_FALSE(delete_route.empty());
            for (const auto* fixed_log : {
                     "Logger::Info(log_context) << \"Admin soft delete user request\";",
                     "Logger::Error(log_context) << \"Failed to soft delete user\";",
                     "Logger::Info(log_context) << \"Admin soft delete user successful\";",
                 }) {
                EXPECT_EQ(CountOccurrences(delete_controller, fixed_log), 1U) << fixed_log;
            }
            for (const auto* raw_boundary_log : {
                     "<< \"Admin soft delete user request: \" << request->getPeerAddr().toIpPort()",
                     "<< \"Failed to soft delete user: \" << result.error().message",
                     "<< \"Admin soft delete user successful: target_id=\" << target_id",
                 }) {
                EXPECT_EQ(CountOccurrences(delete_controller, raw_boundary_log), 0U)
                    << raw_boundary_log;
            }

            EXPECT_EQ(CountOccurrences(delete_controller, "Logger::Info(log_context)"), 2U);
            EXPECT_EQ(CountOccurrences(delete_controller, "Logger::Warn(log_context)"), 0U);
            EXPECT_EQ(CountOccurrences(delete_controller, "Logger::Debug(log_context)"), 0U);
            EXPECT_EQ(CountOccurrences(delete_controller, "Logger::Error(log_context)"), 1U);
            EXPECT_EQ(CountOccurrences(delete_controller, "ErrorCode::ValidationFailed"), 2U);
            for (const auto* preserved_controller_step : {
                     "GetRequestLogContext(request, \"admin\")",
                     "request->attributes()->get<uint64_t>(\"user_id\")",
                     "if (id.empty())",
                     "\"Missing required parameter: id\"",
                     "target_id = std::stoull(id);",
                     "catch (const std::exception&)",
                     "\"Invalid user id format\"",
                     "services::AdminService::GetInstance()",
                     "service->SoftDeleteUser(target_id, operator_id, log_context)",
                     "if (!result)",
                     "co_return Response::Error(result.error());",
                     "co_return Response::Success();",
                 }) {
                EXPECT_EQ(CountOccurrences(delete_controller, preserved_controller_step), 1U)
                    << preserved_controller_step;
            }

            EXPECT_EQ(
                CountOccurrences(delete_route, "\"/api/admin/users/{id}\""),
                1U
            );
            EXPECT_EQ(CountOccurrences(delete_route, "drogon::Delete"), 1U);
            EXPECT_EQ(
                CountOccurrences(delete_route, "\"disk::filters::AdminAuthFilter\""),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    delete_route,
                    "\"disk::filters::AdminRateLimitFilter\""
                ),
                1U
            );
            EXPECT_LT(
                delete_route.find("\"disk::filters::AdminAuthFilter\""),
                delete_route.find("\"disk::filters::AdminRateLimitFilter\"")
            );
        }

        TEST(
            AdminGlobalStorageStatsControllerValueLogContractTest,
            StorageStatsUseFixedSummaries
        ) {
            const auto controller_source =
                ReadSourceFile("src/controllers/AdminController.cpp");
            const auto controller_header =
                ReadSourceFile("src/controllers/AdminController.hpp");
            const auto dto_source = ReadSourceFile("src/dtos/AdminDto.hpp");
            const auto stats_controller = ExtractRange(
                controller_source,
                "auto AdminController::GetGlobalStorageStats(",
                "    auto AdminController::ListShares("
            );
            const auto stats_route = ExtractRange(
                controller_header,
                "ADD_METHOD_TO(\n            AdminController::GetGlobalStorageStats,",
                "        ADD_METHOD_TO(\n            AdminController::ListShares,"
            );
            const auto stats_response = ExtractRange(
                dto_source,
                "struct StorageStatsResponse",
                "    struct SystemStatusResponse"
            );

            ASSERT_FALSE(stats_controller.empty());
            ASSERT_FALSE(stats_route.empty());
            ASSERT_FALSE(stats_response.empty());
            for (const auto* fixed_log : {
                     "Logger::Info(log_context) << \"Admin get global storage stats request\";",
                     "Logger::Error(log_context) << \"Failed to get global storage stats\";",
                     "Logger::Info(log_context) << \"Admin get global storage stats successful\";",
                 }) {
                EXPECT_EQ(CountOccurrences(stats_controller, fixed_log), 1U) << fixed_log;
            }
            for (const auto* raw_boundary_log : {
                     "<< \"Admin get global storage stats request: \"\n            << request->getPeerAddr().toIpPort()",
                     "<< \"Failed to get global storage stats: \" << result.error().message",
                 }) {
                EXPECT_EQ(CountOccurrences(stats_controller, raw_boundary_log), 0U)
                    << raw_boundary_log;
            }

            EXPECT_EQ(CountOccurrences(stats_controller, "Logger::Info(log_context)"), 2U);
            EXPECT_EQ(CountOccurrences(stats_controller, "Logger::Warn(log_context)"), 0U);
            EXPECT_EQ(CountOccurrences(stats_controller, "Logger::Debug(log_context)"), 0U);
            EXPECT_EQ(CountOccurrences(stats_controller, "Logger::Error(log_context)"), 1U);
            EXPECT_EQ(CountOccurrences(stats_controller, "ErrorCode::ValidationFailed"), 0U);
            for (const auto* preserved_controller_step : {
                     "GetRequestLogContext(request, \"admin\")",
                     "request->attributes()->get<uint64_t>(\"user_id\")",
                     "services::AdminService::GetInstance()",
                     "service->GetGlobalStorageStats(operator_id, log_context)",
                     "if (!result)",
                     "co_return Response::Error(result.error());",
                     "co_return Response::Success(result->ToJson());",
                 }) {
                EXPECT_EQ(CountOccurrences(stats_controller, preserved_controller_step), 1U)
                    << preserved_controller_step;
            }

            EXPECT_EQ(
                CountOccurrences(stats_route, "\"/api/admin/storage/stats\""),
                1U
            );
            EXPECT_EQ(CountOccurrences(stats_route, "drogon::Get"), 1U);
            EXPECT_EQ(
                CountOccurrences(stats_route, "\"disk::filters::AdminAuthFilter\""),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(stats_route, "\"disk::filters::AdminRateLimitFilter\""),
                1U
            );
            EXPECT_LT(
                stats_route.find("\"disk::filters::AdminAuthFilter\""),
                stats_route.find("\"disk::filters::AdminRateLimitFilter\"")
            );

            for (const auto* response_field : {
                     "total_users",
                     "total_files",
                     "total_storage_used",
                     "total_storage_quota",
                     "active_shares",
                 }) {
                const auto mapping =
                    std::string("SetField(json, \"") + response_field + "\", " +
                    response_field + ");";
                EXPECT_EQ(CountOccurrences(stats_response, mapping), 1U) << mapping;
            }
        }

        TEST(AdminListSharesControllerValueLogContractTest, ListUsesFixedSummaries) {
            const auto controller_source =
                ReadSourceFile("src/controllers/AdminController.cpp");
            const auto controller_header =
                ReadSourceFile("src/controllers/AdminController.hpp");
            const auto dto_source = ReadSourceFile("src/dtos/AdminDto.hpp");
            const auto list_controller = ExtractRange(
                controller_source,
                "auto AdminController::ListShares(",
                "    auto AdminController::GetShareDetail("
            );
            const auto list_route = ExtractRange(
                controller_header,
                "ADD_METHOD_TO(\n            AdminController::ListShares,",
                "        ADD_METHOD_TO(\n            AdminController::GetShareDetail,"
            );
            const auto list_request = ExtractRange(
                dto_source,
                "struct ListSharesRequest",
                "    struct AdminLogListRequest"
            );
            const auto list_response = ExtractRange(
                dto_source,
                "struct ShareListResponse",
                "    struct AdminLogDetailResponse"
            );

            ASSERT_FALSE(list_controller.empty());
            ASSERT_FALSE(list_route.empty());
            ASSERT_FALSE(list_request.empty());
            ASSERT_FALSE(list_response.empty());
            for (const auto* fixed_log : {
                     "Logger::Info(log_context) << \"Admin list shares request\";",
                     "Logger::Warn(log_context) << \"List shares request validation failed\";",
                     "Logger::Error(log_context) << \"Failed to list shares\";",
                     "Logger::Info(log_context) << \"Admin list shares successful\";",
                 }) {
                EXPECT_EQ(CountOccurrences(list_controller, fixed_log), 1U) << fixed_log;
            }
            for (const auto* raw_boundary_log : {
                     "<< \"Admin list shares request: \" << request->getPeerAddr().toIpPort()",
                     "<< \"List shares request validation failed: \" << parse_result.error().message",
                     "<< \"Failed to list shares: \" << result.error().message",
                 }) {
                EXPECT_EQ(CountOccurrences(list_controller, raw_boundary_log), 0U)
                    << raw_boundary_log;
            }

            EXPECT_EQ(CountOccurrences(list_controller, "Logger::Info(log_context)"), 2U);
            EXPECT_EQ(CountOccurrences(list_controller, "Logger::Warn(log_context)"), 1U);
            EXPECT_EQ(CountOccurrences(list_controller, "Logger::Debug(log_context)"), 0U);
            EXPECT_EQ(CountOccurrences(list_controller, "Logger::Error(log_context)"), 1U);
            for (const auto* preserved_controller_step : {
                     "GetRequestLogContext(request, \"admin\")",
                     "admin::ListSharesRequest::FromRequest(request, log_context)",
                     "if (!parse_result)",
                     "co_return Response::Error(parse_result.error());",
                     "request->attributes()->get<uint64_t>(\"user_id\")",
                     "services::AdminService::GetInstance()",
                     "service->ListShares(*parse_result, operator_id, log_context)",
                     "if (!result)",
                     "co_return Response::Error(result.error());",
                     "co_return Response::Success(result->ToJson());",
                 }) {
                EXPECT_EQ(CountOccurrences(list_controller, preserved_controller_step), 1U)
                    << preserved_controller_step;
            }

            EXPECT_EQ(CountOccurrences(list_route, "\"/api/admin/shares\""), 1U);
            EXPECT_EQ(CountOccurrences(list_route, "drogon::Get"), 1U);
            EXPECT_EQ(
                CountOccurrences(list_route, "\"disk::filters::AdminAuthFilter\""),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(list_route, "\"disk::filters::AdminRateLimitFilter\""),
                1U
            );
            EXPECT_LT(
                list_route.find("\"disk::filters::AdminAuthFilter\""),
                list_route.find("\"disk::filters::AdminRateLimitFilter\"")
            );

            for (const auto* request_parser : {
                     "QueryPositiveInt(req, \"page\", 1)",
                     "QueryPositiveInt(req, \"page_size\", 1, 100)",
                     "req->getParameter(\"status\")",
                     "QueryUInt64(req, \"user_id\")",
                     "req->getParameter(\"username\")",
                 }) {
                EXPECT_EQ(CountOccurrences(list_request, request_parser), 1U)
                    << request_parser;
            }
            EXPECT_EQ(
                CountOccurrences(list_response, "SetArray(json, \"items\", items);"),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    list_response,
                    "SetField(json, \"pagination\", pagination);"
                ),
                1U
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

        TEST(AdminChangeUserRoleContractTest, SeparatesMissingRowsFromDatabaseErrors) {
            const auto service_source = ReadSourceFile("src/services/AdminService.cpp");
            const auto role_body = ExtractRange(
                service_source,
                "auto AdminService::ChangeUserRole(",
                "    auto AdminService::ChangeUserAvailableSpace("
            );

            ASSERT_FALSE(role_body.empty());
            EXPECT_EQ(
                CountOccurrences(
                    role_body,
                    "catch (const drogon::orm::UnexpectedRows&)"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    role_body,
                    "catch (const drogon::orm::DrogonDbException&)"
                ),
                2U
            );

            const auto missing_catch = role_body.find(
                "catch (const drogon::orm::UnexpectedRows&)"
            );
            const auto database_catch = role_body.rfind(
                "catch (const drogon::orm::DrogonDbException&)"
            );
            EXPECT_NE(missing_catch, std::string::npos);
            EXPECT_NE(database_catch, std::string::npos);
            EXPECT_LT(missing_catch, database_catch);

            EXPECT_EQ(
                CountOccurrences(
                    role_body,
                    "ErrorInfo(ErrorCode::AdminUserNotFound)"
                ),
                1U
            );
            EXPECT_EQ(CountOccurrences(role_body, ".what()"), 0U);
            EXPECT_EQ(CountOccurrences(role_body, "error_msg.find("), 0U);
            EXPECT_EQ(
                CountOccurrences(
                    role_body,
                    "Logger::Error(log_context)\n                        << \"Admin count administrators database error\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    role_body,
                    "Logger::Error(log_context)\n                << \"Admin change user role database error\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    role_body,
                    "ErrorCode::InternalError,\n                        \"Failed to verify admin count\""
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    role_body,
                    "ErrorCode::InternalError,\n                \"Failed to change user role\""
                ),
                1U
            );

            EXPECT_EQ(
                CountOccurrences(
                    role_body,
                    "if (role == 0 && user.getValueOfRole() == 1)"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    role_body,
                    "SELECT COUNT(*) AS cnt FROM users WHERE role = 1"
                ),
                1U
            );
            EXPECT_EQ(CountOccurrences(role_body, "if (admin_count <= 1)"), 1U);
            EXPECT_EQ(
                CountOccurrences(
                    role_body,
                    "ErrorInfo(ErrorCode::AdminCannotDemoteLast)"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    role_body,
                    "user.setRole(static_cast<int8_t>(role));"
                ),
                1U
            );
            EXPECT_EQ(CountOccurrences(role_body, "co_await mapper.update(user);"), 1U);
            EXPECT_EQ(
                CountOccurrences(role_body, "\"admin.user.role_change\""),
                1U
            );
        }

        TEST(AdminChangeUserAvailableSpaceContractTest, SeparatesMissingRowsFromDatabaseErrors) {
            const auto service_source = ReadSourceFile("src/services/AdminService.cpp");
            const auto available_space_body = ExtractRange(
                service_source,
                "auto AdminService::ChangeUserAvailableSpace(",
                "    auto AdminService::SoftDeleteUser("
            );

            ASSERT_FALSE(available_space_body.empty());
            EXPECT_EQ(
                CountOccurrences(
                    available_space_body,
                    "catch (const drogon::orm::UnexpectedRows&)"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    available_space_body,
                    "catch (const drogon::orm::DrogonDbException&)"
                ),
                1U
            );

            const auto missing_catch = available_space_body.find(
                "catch (const drogon::orm::UnexpectedRows&)"
            );
            const auto database_catch = available_space_body.find(
                "catch (const drogon::orm::DrogonDbException&)"
            );
            EXPECT_NE(missing_catch, std::string::npos);
            EXPECT_NE(database_catch, std::string::npos);
            EXPECT_LT(missing_catch, database_catch);

            EXPECT_EQ(
                CountOccurrences(
                    available_space_body,
                    "ErrorInfo(ErrorCode::AdminUserNotFound)"
                ),
                1U
            );
            EXPECT_EQ(CountOccurrences(available_space_body, ".what()"), 0U);
            EXPECT_EQ(
                CountOccurrences(available_space_body, "error_msg.find("),
                0U
            );
            EXPECT_EQ(
                CountOccurrences(
                    available_space_body,
                    "Logger::Error(log_context)\n                << \"Admin change user available space database error\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    available_space_body,
                    "ErrorCode::InternalError,\n                \"Failed to change user available space\""
                ),
                1U
            );

            EXPECT_EQ(
                CountOccurrences(
                    available_space_body,
                    "constexpr uint64_t bytes_per_g = 1024ULL * 1024ULL * 1024ULL;"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    available_space_body,
                    "available_space_g > std::numeric_limits<uint64_t>::max() / bytes_per_g"
                ),
                1U
            );
            EXPECT_TRUE(Contains(
                available_space_body,
                "storage_used + storage_reserved > std::numeric_limits<uint64_t>::max() - available_space_bytes"
            ));
            EXPECT_EQ(
                CountOccurrences(
                    available_space_body,
                    "const auto new_storage_quota = storage_used + storage_reserved + available_space_bytes;"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    available_space_body,
                    "user.setStorageQuota(new_storage_quota);"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(available_space_body, "co_await mapper.update(user);"),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    available_space_body,
                    "response.storage_quota = new_storage_quota;"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    available_space_body,
                    "\"admin.user.available_space_set\""
                ),
                1U
            );
        }

        TEST(AdminSoftDeleteUserContractTest, SeparatesMissingRowsFromDatabaseErrors) {
            const auto service_source = ReadSourceFile("src/services/AdminService.cpp");
            const auto delete_body = ExtractRange(
                service_source,
                "auto AdminService::SoftDeleteUser(",
                "    auto AdminService::GetGlobalStorageStats("
            );

            ASSERT_FALSE(delete_body.empty());
            EXPECT_EQ(
                CountOccurrences(
                    delete_body,
                    "catch (const drogon::orm::UnexpectedRows&)"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    delete_body,
                    "catch (const drogon::orm::DrogonDbException&)"
                ),
                1U
            );

            const auto missing_catch = delete_body.find(
                "catch (const drogon::orm::UnexpectedRows&)"
            );
            const auto database_catch = delete_body.find(
                "catch (const drogon::orm::DrogonDbException&)"
            );
            EXPECT_NE(missing_catch, std::string::npos);
            EXPECT_NE(database_catch, std::string::npos);
            EXPECT_LT(missing_catch, database_catch);

            EXPECT_EQ(
                CountOccurrences(
                    delete_body,
                    "ErrorInfo(ErrorCode::AdminUserNotFound)"
                ),
                1U
            );
            EXPECT_EQ(CountOccurrences(delete_body, ".what()"), 0U);
            EXPECT_EQ(CountOccurrences(delete_body, "error_msg.find("), 0U);
            EXPECT_EQ(
                CountOccurrences(
                    delete_body,
                    "Logger::Error(log_context)\n                << \"Admin soft delete user database error\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    delete_body,
                    "ErrorCode::InternalError,\n                \"Failed to soft delete user\""
                ),
                1U
            );

            EXPECT_EQ(
                CountOccurrences(
                    delete_body,
                    "user.setStatus(static_cast<int8_t>(0));"
                ),
                1U
            );
            EXPECT_EQ(CountOccurrences(delete_body, "co_await mapper.update(user);"), 1U);
            EXPECT_EQ(
                CountOccurrences(delete_body, "\"admin.user.soft_delete\""),
                1U
            );
        }

        TEST(AdminGlobalStorageStatsContractTest, DatabaseExceptionUsesFixedSummary) {
            const auto service_source = ReadSourceFile("src/services/AdminService.cpp");
            const auto stats_body = ExtractRange(
                service_source,
                "auto AdminService::GetGlobalStorageStats(",
                "    auto AdminService::ListShares("
            );

            ASSERT_FALSE(stats_body.empty());
            EXPECT_EQ(CountOccurrences(stats_body, ".what()"), 0U);
            EXPECT_EQ(
                CountOccurrences(
                    stats_body,
                    "catch (const drogon::orm::DrogonDbException&)"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    stats_body,
                    "Logger::Error(log_context)\n                << \"Admin get global storage stats database error\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    stats_body,
                    "ErrorCode::InternalError,\n                \"Failed to get global storage stats\""
                ),
                1U
            );

            EXPECT_EQ(CountOccurrences(stats_body, "execSqlCoro("), 3U);
            EXPECT_TRUE(Contains(stats_body, "COUNT(*) AS total_users"));
            EXPECT_TRUE(Contains(stats_body, "COALESCE(SUM(storage_used), 0)"));
            EXPECT_TRUE(Contains(stats_body, "COALESCE(SUM(storage_quota), 0)"));
            EXPECT_TRUE(Contains(stats_body, "COUNT(*) AS total_files FROM files"));
            EXPECT_TRUE(Contains(
                stats_body,
                "COUNT(*) AS active_shares FROM shares WHERE status = 1"
            ));
            for (const auto* field : {
                     "response.total_users =",
                     "response.total_storage_used =",
                     "response.total_storage_quota =",
                     "response.total_files =",
                     "response.active_shares =",
                 }) {
                EXPECT_EQ(CountOccurrences(stats_body, field), 1U) << field;
            }
            EXPECT_EQ(
                CountOccurrences(stats_body, "\"admin.storage.global_stats\""),
                1U
            );
        }

        TEST(AdminOverviewStatsContractTest, DatabaseExceptionUsesFixedSummary) {
            const auto service_source = ReadSourceFile("src/services/AdminService.cpp");
            const auto stats_body = ExtractRange(
                service_source,
                "auto AdminService::GetOverviewStats(",
                "    auto AdminService::GetSystemStatus("
            );

            ASSERT_FALSE(stats_body.empty());
            EXPECT_EQ(CountOccurrences(stats_body, ".what()"), 0U);
            EXPECT_EQ(
                CountOccurrences(
                    stats_body,
                    "catch (const drogon::orm::DrogonDbException&)"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    stats_body,
                    "Logger::Error(log_context) << \"admin.stats.overview database error\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    stats_body,
                    "ErrorCode::InternalError,\n                \"Failed to get overview stats\""
                ),
                1U
            );

            EXPECT_EQ(CountOccurrences(stats_body, "execSqlCoro("), 3U);
            EXPECT_TRUE(Contains(stats_body, "COUNT(*) AS total_users"));
            EXPECT_TRUE(Contains(stats_body, "COALESCE(SUM(storage_used), 0)"));
            EXPECT_TRUE(Contains(stats_body, "COALESCE(SUM(storage_quota), 0)"));
            EXPECT_TRUE(Contains(stats_body, "COUNT(*) AS total_files FROM files"));
            EXPECT_TRUE(Contains(
                stats_body,
                "COUNT(*) AS active_shares FROM shares WHERE status = 1"
            ));
            for (const auto* field : {
                     "response.total_users =",
                     "response.total_storage_used =",
                     "response.total_storage_quota =",
                     "response.total_files =",
                     "response.active_shares =",
                 }) {
                EXPECT_EQ(CountOccurrences(stats_body, field), 1U) << field;
            }
            EXPECT_EQ(
                CountOccurrences(
                    stats_body,
                    "Logger::Info(log_context) << \"admin.stats.overview successful\";"
                ),
                1U
            );
        }

        TEST(AdminSystemStatusContractTest, ProbeExceptionsUseFixedSummaries) {
            const auto service_source = ReadSourceFile("src/services/AdminService.cpp");
            const auto status_body = ExtractRange(
                service_source,
                "auto AdminService::GetSystemStatus(",
                "    auto AdminService::GetAdminLogs("
            );

            ASSERT_FALSE(status_body.empty());
            EXPECT_EQ(CountOccurrences(status_body, ".what()"), 0U);
            for (const auto* catch_clause : {
                     "catch (const drogon::orm::DrogonDbException&)",
                     "catch (const drogon::nosql::RedisException&)",
                     "catch (const std::exception&)",
                     "catch (const std::filesystem::filesystem_error&)",
                 }) {
                EXPECT_EQ(CountOccurrences(status_body, catch_clause), 1U)
                    << catch_clause;
            }
            EXPECT_EQ(
                CountOccurrences(
                    status_body,
                    "Logger::Warn(log_context) << \"admin.stats.system Database check failed\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    status_body,
                    "Logger::Warn(log_context) << \"admin.stats.system Redis check failed\";"
                ),
                2U
            );
            EXPECT_EQ(
                CountOccurrences(
                    status_body,
                    "Logger::Warn(log_context) << \"admin.stats.system disk space check failed\";"
                ),
                1U
            );

            EXPECT_EQ(
                CountOccurrences(status_body, "execSqlCoro(\"SELECT 1\")"),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(status_body, "response.db_connected = true;"),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(status_body, "response.db_connected = false;"),
                1U
            );
            EXPECT_EQ(CountOccurrences(status_body, "getRedisClient()"), 1U);
            EXPECT_EQ(
                CountOccurrences(status_body, "RedisService::Initialize(redis_client);"),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    status_body,
                    "RedisService::GetInstance()->Ping(log_context)"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    status_body,
                    "response.redis_connected = result.has_value() && *result;"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(status_body, "response.redis_connected = false;"),
                3U
            );

            EXPECT_EQ(
                CountOccurrences(status_body, "GetStorageBasePath()"),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(status_body, "std::filesystem::space(storage_path)"),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(status_body, "response.disk_total = space_info.capacity;"),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(status_body, "response.disk_free = space_info.available;"),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    status_body,
                    "response.disk_used = space_info.capacity - space_info.available;"
                ),
                1U
            );
            for (const auto* field : {
                     "response.disk_total = 0;",
                     "response.disk_used = 0;",
                     "response.disk_free = 0;",
                 }) {
                EXPECT_EQ(CountOccurrences(status_body, field), 1U) << field;
            }
            EXPECT_EQ(
                CountOccurrences(status_body, "response.uptime_seconds ="),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    status_body,
                    "Logger::Info(log_context) << \"admin.stats.system successful\";"
                ),
                1U
            );
        }

        TEST(AdminLogListContractTest, DatabaseExceptionUsesFixedSummary) {
            const auto service_source = ReadSourceFile("src/services/AdminService.cpp");
            const auto list_body = ExtractRange(
                service_source,
                "auto AdminService::GetAdminLogs(",
                "    auto AdminService::LogOperation("
            );

            ASSERT_FALSE(list_body.empty());
            EXPECT_EQ(CountOccurrences(list_body, ".what()"), 0U);
            EXPECT_EQ(
                CountOccurrences(
                    list_body,
                    "catch (const drogon::orm::DrogonDbException&)"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    list_body,
                    "Logger::Error(log_context) << \"Admin list logs database error\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    list_body,
                    "ErrorCode::InternalError,\n                \"Failed to list operation logs\""
                ),
                1U
            );

            EXPECT_EQ(CountOccurrences(list_body, "execSqlCoro("), 2U);
            EXPECT_EQ(CountOccurrences(list_body, "FILTER_SQL"), 3U);
            for (const auto* predicate : {
                     "($1::boolean = FALSE OR action = $2)",
                     "($3::boolean = FALSE OR created_at >= $4::date)",
                     "($5::boolean = FALSE OR created_at < $6::date + INTERVAL '1 day')",
                     "($7::boolean = FALSE OR target_type = $8)",
                     "($9::boolean = FALSE OR target_name = $10)",
                 }) {
                EXPECT_EQ(CountOccurrences(list_body, predicate), 1U)
                    << predicate;
            }
            EXPECT_TRUE(Contains(
                list_body,
                "SELECT COUNT(*) AS total FROM operation_logs"
            ));
            EXPECT_TRUE(Contains(
                list_body,
                "SELECT id, user_id, action, target_type, target_id, target_name, details, ip_address, created_at FROM operation_logs"
            ));
            EXPECT_TRUE(Contains(
                list_body,
                "ORDER BY created_at DESC, id DESC LIMIT $11 OFFSET $12"
            ));
            EXPECT_EQ(CountOccurrences(list_body, "static_cast<int64_t>("), 3U);

            for (const auto* field : {
                     "response.pagination.page = req.page;",
                     "response.pagination.page_size = req.page_size;",
                     "response.pagination.total = total;",
                     "response.pagination.total_pages = total_pages;",
                     "log.id = row[\"id\"].as<uint64_t>();",
                     "log.user_id = row[\"user_id\"].isNull()",
                     "log.action = row[\"action\"].as<std::string>();",
                     "log.target_type = row[\"target_type\"].isNull()",
                     "log.target_id = row[\"target_id\"].isNull()",
                     "log.target_name = row[\"target_name\"].isNull()",
                     "log.details = row[\"details\"].isNull()",
                     "log.ip_address = row[\"ip_address\"].as<std::string>();",
                     "log.created_at = row[\"created_at\"].as<std::string>();",
                     "response.items.push_back(std::move(log));",
                 }) {
                EXPECT_EQ(CountOccurrences(list_body, field), 1U) << field;
            }
            EXPECT_EQ(
                CountOccurrences(
                    list_body,
                    "Logger::Info(log_context) << \"Admin list logs successful: total=\" << total;"
                ),
                1U
            );
        }

        TEST(AdminAuditWriteContractTest, DatabaseExceptionUsesFixedSummary) {
            const auto service_source = ReadSourceFile("src/services/AdminService.cpp");
            const auto audit_body = ExtractRange(
                service_source,
                "auto AdminService::LogOperation(",
                "\n} // namespace disk::services"
            );

            ASSERT_FALSE(audit_body.empty());
            EXPECT_EQ(CountOccurrences(audit_body, ".what()"), 0U);
            EXPECT_EQ(
                CountOccurrences(
                    audit_body,
                    "catch (const drogon::orm::DrogonDbException&)"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    audit_body,
                    "Logger::Error(log_context) << \"Failed to log operation\";"
                ),
                1U
            );

            EXPECT_EQ(
                CountOccurrences(
                    audit_body,
                    "disk::utils::SetRequestCorrelationFields(details, log_context);"
                ),
                1U
            );
            EXPECT_EQ(CountOccurrences(audit_body, "execSqlCoro("), 1U);
            EXPECT_TRUE(Contains(
                audit_body,
                "INSERT INTO operation_logs (user_id, action, target_type, target_id, target_name, details, ip_address)"
            ));
            EXPECT_TRUE(Contains(
                audit_body,
                "VALUES ($1, $2, $3, $4, $5, $6, 'system')"
            ));
            EXPECT_EQ(
                CountOccurrences(
                    audit_body,
                    "operator_id,\n                action,\n                target_type,\n                target_id,\n                target_name,\n                SerializeDetails(details)"
                ),
                1U
            );
            EXPECT_EQ(CountOccurrences(audit_body, "SerializeDetails(details)"), 1U);
            EXPECT_EQ(
                CountOccurrences(
                    audit_body,
                    "Logger::Debug(log_context)\n                << \"Operation logged: \" << action << \" by user_id=\" << operator_id;"
                ),
                1U
            );
            EXPECT_EQ(CountOccurrences(audit_body, "throw;"), 0U);
            EXPECT_EQ(CountOccurrences(audit_body, "std::unexpected"), 0U);
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
