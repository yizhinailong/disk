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

        auto CallContainsContext(const std::string& source, std::string_view call_marker) -> bool {
            const auto begin = source.find(call_marker);
            if (begin == std::string::npos) {
                return false;
            }
            const auto end = source.find(");", begin);
            return end != std::string::npos &&
                   source.substr(begin, end - begin).find("log_context") != std::string::npos;
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
                7
            );
            EXPECT_EQ(
                CountOccurrences(service_request_body, "disk::utils::LogContext log_context"),
                7
            );
            for (const auto* call_marker : {
                     "co_await FindUser(",
                     "co_await IncrementLoginAttempts(",
                     "co_await UpdateLoginInfo(",
                     "TokenService::GetInstance()->GenerateTokens(",
                     "TokenService::GetInstance()->VerifyRefreshToken(",
                     "TokenService::GetInstance()->RefreshRefreshToken(",
                     "TokenService::GetInstance()->InvalidateAccessToken(",
                 }) {
                EXPECT_TRUE(CallContainsContext(service_request_body, call_marker));
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

    } // namespace
} // namespace disk::auth
