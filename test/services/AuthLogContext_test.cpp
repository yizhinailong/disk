/**
 * @file AuthLogContext_test.cpp
 * @brief Authentication request correlation source contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace disk::auth {
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

        auto ExtractBetween(
            const std::string& source,
            std::string_view begin_marker,
            std::string_view end_marker
        ) -> std::string {
            const auto begin = source.find(begin_marker);
            if (begin == std::string::npos) {
                return {};
            }
            const auto end = source.find(end_marker, begin + begin_marker.size());
            if (end == std::string::npos) {
                return {};
            }
            return source.substr(begin, end - begin);
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

        TEST(AuthImplementationHelperContractTest, UserResponseMapperHasInternalLinkage) {
            const auto service_header = ReadSourceFile("src/services/AuthService.hpp");
            const auto service_source = ReadSourceFile("src/services/AuthService.cpp");

            EXPECT_FALSE(Contains(service_header, "UserToResponse("));
            EXPECT_FALSE(Contains(service_source, "AuthService::UserToResponse("));
            const auto anonymous_namespace = service_source.find("namespace {");
            const auto helper = service_source.find("[[nodiscard]] auto UserToResponse(");
            const auto anonymous_namespace_end =
                service_source.find("} // namespace", anonymous_namespace);
            ASSERT_NE(anonymous_namespace, std::string::npos);
            ASSERT_NE(helper, std::string::npos);
            ASSERT_NE(anonymous_namespace_end, std::string::npos);
            EXPECT_LT(anonymous_namespace, helper);
            EXPECT_LT(helper, anonymous_namespace_end);
            EXPECT_TRUE(Contains(
                service_source,
                "[[nodiscard]] auto UserToResponse(const Users& user) -> RegisterResponse"
            ));
            EXPECT_EQ(CountOccurrences(service_source, "UserToResponse("), 3U);
            EXPECT_TRUE(Contains(service_source, "response.id = user.getValueOfId()"));
            EXPECT_TRUE(Contains(
                service_source,
                "response.username = user.getValueOfUsername()"
            ));
            EXPECT_TRUE(Contains(service_source, "response.email = user.getValueOfEmail()"));
            EXPECT_TRUE(Contains(
                service_source,
                "response.nickname = user.getNickname() ? *user.getNickname() : user.getValueOfUsername()"
            ));
            EXPECT_TRUE(Contains(
                service_source,
                "response.storage_quota = user.getValueOfStorageQuota()"
            ));
            EXPECT_TRUE(Contains(
                service_source,
                "response.storage_used = user.getValueOfStorageUsed()"
            ));
            EXPECT_TRUE(Contains(
                service_source,
                "response.created_at = user.getValueOfCreatedAt().toDbStringLocal()"
            ));
        }

        TEST(AuthLogContextContractTest, ControllerDtoAndServiceUseExplicitRequestContext) {
            const auto controller_source = ReadSourceFile("src/controllers/AuthController.cpp");
            const auto dto_source = ReadSourceFile("src/dtos/AuthDto.hpp");
            const auto service_header = ReadSourceFile("src/services/AuthService.hpp");
            const auto service_source = ReadSourceFile("src/services/AuthService.cpp");
            const auto service_request_body =
                ExtractFrom(service_source, "auto AuthService::Register(");

            ASSERT_FALSE(controller_source.empty());
            ASSERT_FALSE(dto_source.empty());
            ASSERT_FALSE(service_header.empty());
            ASSERT_FALSE(service_request_body.empty());

            EXPECT_EQ(
                CountOccurrences(
                    controller_source,
                    "GetRequestLogContext(request, \"auth\")"
                ),
                4
            );
            for (const auto* call_marker : {
                     "RegisterRequest::FromRequest(",
                     "LoginRequest::FromRequest(",
                     "RefreshTokenRequest::FromRequest(",
                     "m_auth_service->Register(",
                     "m_auth_service->Login(",
                     "m_auth_service->RefreshTokens(",
                     "m_auth_service->Logout(",
                 }) {
                EXPECT_TRUE(CallContainsContext(controller_source, call_marker));
            }

            EXPECT_EQ(CountOccurrences(dto_source, "disk::utils::LogContext log_context = {}"), 3);
            EXPECT_EQ(
                CountOccurrences(service_header, "disk::utils::LogContext log_context = {}"),
                4
            );
            EXPECT_EQ(
                CountOccurrences(service_header, "disk::utils::LogContext log_context"),
                8
            );
            EXPECT_EQ(
                CountOccurrences(service_request_body, "disk::utils::LogContext log_context"),
                8
            );
            for (const auto* call_marker : {
                     "co_await FindUser(",
                     "co_await ValidateAccountAccess(",
                     "co_await IncrementLoginAttempts(",
                     "co_await UpdateLoginInfo(",
                     "TokenService::GetInstance()->GenerateTokens(",
                     "TokenService::GetInstance()->VerifyRefreshToken(",
                     "TokenService::GetInstance()->RefreshRefreshToken(",
                     "TokenService::GetInstance()->InvalidateAccessToken(",
                 }) {
                EXPECT_TRUE(CallContainsContext(service_request_body, call_marker));
            }

            EXPECT_EQ(CountOccurrences(service_request_body, ".what()"), 0U);
            EXPECT_EQ(CountOccurrences(service_request_body, ".error().message"), 0U);
            for (const auto* fixed_failure : {
                     "Logger::Error(log_context) << \"Uniqueness check failed\";",
                     "Logger::Error(log_context) << \"User registration database insert failed\";",
                     "Logger::Warn(log_context) << \"Redis rate limit check failed\";",
                     "Logger::Error(log_context) << \"User query failed\";",
                     "Logger::Error(log_context) << \"Token refresh processing failed\";",
                     "Logger::Warn(log_context) << \"Failed to record logout log\";",
                     "Logger::Warn(log_context) << \"User lookup failed\";",
                     "Logger::Error(log_context) << \"Failed to validate account access\";",
                     "Logger::Warn(log_context) << \"Failed to clear login rate limit counter\";",
                     "Logger::Error(log_context) << \"Failed to update login info\";",
                     "Logger::Error(log_context) << \"Failed to increment login attempts\";",
                 }) {
                EXPECT_TRUE(Contains(service_request_body, fixed_failure)) << fixed_failure;
            }

            for (const auto* body : {
                     &controller_source,
                     &dto_source,
                     &service_request_body,
                 }) {
                EXPECT_FALSE(Contains(*body, "Logger::Debug()"));
                EXPECT_FALSE(Contains(*body, "Logger::Info()"));
                EXPECT_FALSE(Contains(*body, "Logger::Warn()"));
                EXPECT_FALSE(Contains(*body, "Logger::Error()"));
                EXPECT_FALSE(Contains(*body, "<< request.password"));
                EXPECT_FALSE(Contains(*body, "<< request.refresh_token"));
                EXPECT_FALSE(Contains(*body, "<< access_token"));
                EXPECT_FALSE(Contains(*body, "<< auth_header"));
            }
        }

        TEST(AuthRegistrationValueLogContractTest, RegisterUsesFixedSummaries) {
            const auto service_source = ReadSourceFile("src/services/AuthService.cpp");
            const auto register_body = ExtractBetween(
                service_source,
                "auto AuthService::Register(",
                "auto AuthService::Login("
            );

            ASSERT_FALSE(register_body.empty());
            for (const auto* fixed_log : {
                     "Logger::Debug(log_context) << \"User registration started\";",
                     "Logger::Warn(log_context) << \"Registration username already exists\";",
                     "Logger::Warn(log_context) << \"Registration email already exists\";",
                     "Logger::Debug(log_context) << \"Registration password hash started\";",
                     "Logger::Error(log_context) << \"Registration password hash failed\";",
                     "Logger::Debug(log_context) << \"Registration password hash completed\";",
                     "Logger::Info(log_context) << \"User registration data inserted\";",
                     "Logger::Info(log_context) << \"User registration completed\";",
                 }) {
                EXPECT_EQ(CountOccurrences(register_body, fixed_log), 1U) << fixed_log;
            }
            for (const auto* raw_account_log : {
                     "\"Starting user registration: \" << request.username",
                     "\"Username already exists: \" << request.username",
                     "\"Email already exists: \" << request.email.substr(0, 3) << \"***@***\"",
                     "\"Starting password hash: \" << request.username",
                     "\"Password hash failed: \" << request.username",
                     "\"Password hash completed: \" << request.username",
                     "<< \"User data inserted successfully: \" << request.username",
                     "<< \"User registration process completed: \" << response.username",
                 }) {
                EXPECT_EQ(CountOccurrences(register_body, raw_account_log), 0U)
                    << raw_account_log;
            }

            for (const auto* preserved_registration : {
                     "COUNT(CASE WHEN username = $1 THEN 1 END)",
                     "COUNT(CASE WHEN email = $2 THEN 1 END)",
                     "co_await RunOnAuthCpuPool(",
                     "HashUtil::HashPassword(password)",
                     "user.setUsername(request.username);",
                     "user.setEmail(request.email);",
                     "user.setPasswordHash(hash_result.value());",
                     "user.setNickname(request.username);",
                     "user.setStorageQuota(DEFAULT_STORAGE_QUOTA);",
                     "user.setStorageUsed(0);",
                     "user.setStatus(1);",
                     "user.setLoginAttempts(0);",
                     "user = co_await mapper.insert(user);",
                     "auto response = UserToResponse(user);",
                     "co_return response;",
                 }) {
                EXPECT_EQ(CountOccurrences(register_body, preserved_registration), 1U)
                    << preserved_registration;
            }
            EXPECT_EQ(CountOccurrences(register_body, "ErrorCode::UsernameExists"), 1U);
            EXPECT_EQ(CountOccurrences(register_body, "ErrorCode::EmailExists"), 1U);
        }

        TEST(AuthRegistrationDtoValueLogContractTest, RegisterRequestUsesFixedSummaries) {
            const auto dto_source = ReadSourceFile("src/dtos/AuthDto.hpp");
            const auto register_request = ExtractBetween(
                dto_source,
                "struct RegisterRequest",
                "struct LoginRequest"
            );

            ASSERT_FALSE(register_request.empty());
            for (const auto* fixed_log : {
                     "Logger::Debug(log_context) << \"Register request fields parsed\";",
                     "Logger::Warn(log_context) << \"Register username validation failed\";",
                     "Logger::Warn(log_context) << \"Register email validation failed\";",
                     "Logger::Warn(log_context) << \"Register password validation failed\";",
                 }) {
                EXPECT_EQ(CountOccurrences(register_request, fixed_log), 1U) << fixed_log;
            }
            for (const auto* raw_field_log : {
                     "<< \"Parsed register request: \" << request.username",
                     "<< \"Username format error: \" << request.username",
                     "<< \"Email format error: \" << request.email",
                     "<< \"Password format error: \" << request.username",
                 }) {
                EXPECT_EQ(CountOccurrences(register_request, raw_field_log), 0U)
                    << raw_field_log;
            }

            for (const auto* preserved_parse_step : {
                     "auto json_result = RequireJsonBody(req);",
                     "auto username_result = RequireString(json, \"username\");",
                     "auto email_result = RequireString(json, \"email\");",
                     "auto password_result = RequireString(json, \"password\");",
                     "request.username = std::move(*username_result);",
                     "request.email = std::move(*email_result);",
                     "request.password = std::move(*password_result);",
                     "if (!request.ValidateUsername())",
                     "if (!request.ValidateEmail())",
                     "if (!request.ValidatePassword())",
                     "ErrorInfo(ErrorCode::ValidationFailed, \"Username format error\")",
                     "ErrorInfo(ErrorCode::ValidationFailed, \"Email format error\")",
                     "ErrorInfo(ErrorCode::ValidationFailed, \"Password format error\")",
                     "return request;",
                 }) {
                EXPECT_EQ(CountOccurrences(register_request, preserved_parse_step), 1U)
                    << preserved_parse_step;
            }
            EXPECT_EQ(
                CountOccurrences(
                    register_request,
                    "Logger::Debug(log_context) << \"Start parsing register request parameters\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    register_request,
                    "Logger::Debug(log_context) << \"Request parameters validated\";"
                ),
                1U
            );
        }

        TEST(AuthLoginDtoValueLogContractTest, LoginRequestUsesFixedSummary) {
            const auto dto_source = ReadSourceFile("src/dtos/AuthDto.hpp");
            const auto login_request = ExtractBetween(
                dto_source,
                "struct LoginRequest",
                "struct RefreshTokenRequest"
            );

            ASSERT_FALSE(login_request.empty());
            EXPECT_EQ(
                CountOccurrences(
                    login_request,
                    "Logger::Debug(log_context) << \"Login request fields parsed\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    login_request,
                    "Logger::Debug(log_context) << \"Parsed login request: \" << request.account;"
                ),
                0U
            );

            for (const auto* preserved_parse_step : {
                     "auto json_result = RequireJsonBody(req);",
                     "auto account_result = RequireString(json, \"account\");",
                     "auto password_result = RequireString(json, \"password\");",
                     "request.account = std::move(*account_result);",
                     "request.password = std::move(*password_result);",
                     "if (request.account.empty())",
                     "if (request.password.empty())",
                     "ErrorInfo(ErrorCode::ValidationFailed, \"Account cannot be empty\")",
                     "ErrorInfo(ErrorCode::ValidationFailed, \"Password cannot be empty\")",
                     "return request;",
                 }) {
                EXPECT_EQ(CountOccurrences(login_request, preserved_parse_step), 1U)
                    << preserved_parse_step;
            }
            EXPECT_EQ(
                CountOccurrences(
                    login_request,
                    "Logger::Debug(log_context) << \"Start parsing login request parameters\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    login_request,
                    "Logger::Warn(log_context) << \"Account cannot be empty\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    login_request,
                    "Logger::Warn(log_context) << \"Password cannot be empty\";"
                ),
                1U
            );
        }

    } // namespace
} // namespace disk::auth
