/**
 * @file UserLogContext_test.cpp
 * @brief User profile request correlation source contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace disk::user {
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

        TEST(UserLogContextContractTest, ControllerDtoAndServiceUseExplicitRequestContext) {
            const auto controller_source = ReadSourceFile("src/controllers/UserController.cpp");
            const auto dto_source = ReadSourceFile("src/dtos/UserDto.hpp");
            const auto service_header = ReadSourceFile("src/services/UserService.hpp");
            const auto service_source = ReadSourceFile("src/services/UserService.cpp");
            const auto service_request_body =
                ExtractFrom(service_source, "auto UserService::GetProfile(");
            const auto change_password_body = ExtractBetween(
                service_source,
                "auto UserService::ChangePassword(",
                "auto UserService::UpdateProfile("
            );
            const auto update_profile_body = ExtractBetween(
                service_source,
                "auto UserService::UpdateProfile(",
                "auto UserService::GetStorage("
            );

            ASSERT_FALSE(controller_source.empty());
            ASSERT_FALSE(dto_source.empty());
            ASSERT_FALSE(service_header.empty());
            ASSERT_FALSE(service_request_body.empty());
            ASSERT_FALSE(change_password_body.empty());
            ASSERT_FALSE(update_profile_body.empty());

            EXPECT_EQ(
                CountOccurrences(
                    controller_source,
                    "GetRequestLogContext(request, \"user\")"
                ),
                4
            );
            for (const auto* call_marker : {
                     "ChangePasswordRequest::FromRequest(",
                     "UpdateProfileRequest::FromRequest(",
                     "m_user_service->GetProfile(",
                     "m_user_service->ChangePassword(",
                     "m_user_service->UpdateProfile(",
                     "m_user_service->GetStorage(",
                 }) {
                EXPECT_TRUE(CallContainsContext(controller_source, call_marker));
            }

            EXPECT_EQ(CountOccurrences(dto_source, "disk::utils::LogContext log_context = {}"), 2);
            EXPECT_EQ(
                CountOccurrences(service_header, "disk::utils::LogContext log_context = {}"),
                4
            );
            EXPECT_EQ(
                CountOccurrences(
                    service_request_body,
                    "disk::utils::LogContext log_context"
                ),
                4
            );

            EXPECT_EQ(CountOccurrences(service_request_body, ".what()"), 0U);
            EXPECT_EQ(CountOccurrences(service_request_body, ".find(\"condition\")"), 0U);
            EXPECT_EQ(CountOccurrences(service_request_body, ".find(\"empty\")"), 0U);
            EXPECT_EQ(
                CountOccurrences(
                    service_request_body,
                    "catch (const drogon::orm::UnexpectedRows&)"
                ),
                2U
            );
            for (const auto* body : { &change_password_body, &update_profile_body }) {
                const auto unexpected_rows =
                    body->find("catch (const drogon::orm::UnexpectedRows&)");
                const auto database_error =
                    body->find("catch (const drogon::orm::DrogonDbException&)");
                EXPECT_LT(unexpected_rows, database_error);
            }
            for (const auto* fixed_failure : {
                     "Logger::Error(log_context) << \"Get user profile database error\";",
                     "Logger::Error(log_context) << \"Get user profile processing failed\";",
                     "Logger::Error(log_context) << \"Change password database error\";",
                     "Logger::Error(log_context) << \"Change password processing failed\";",
                     "Logger::Error(log_context) << \"Update user profile database error\";",
                     "Logger::Error(log_context) << \"Update user profile processing failed\";",
                     "Logger::Error(log_context) << \"Failed to get storage stats\";",
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
            }
        }

    } // namespace
} // namespace disk::user
