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

        TEST(UserProfileValueLogContractTest, UpdateUsesFixedSummaries) {
            const auto service_source = ReadSourceFile("src/services/UserService.cpp");
            const auto update_profile_body = ExtractBetween(
                service_source,
                "auto UserService::UpdateProfile(",
                "auto UserService::GetStorage("
            );

            ASSERT_FALSE(update_profile_body.empty());
            EXPECT_EQ(CountOccurrences(update_profile_body, "Logger::Debug(log_context)"), 3U);
            for (const auto* fixed_message : {
                     "Logger::Debug(log_context) << \"User profile target loaded\";",
                     "Logger::Debug(log_context) << \"User profile nickname update selected\";",
                     "Logger::Debug(log_context) << \"User profile avatar update selected\";",
                 }) {
                EXPECT_EQ(CountOccurrences(update_profile_body, fixed_message), 1U)
                    << fixed_message;
            }
            for (const auto* raw_profile_log : {
                     "<< \"Found user: \" << user.getValueOfUsername()",
                     "<< \"Updating nickname: \" << *request.nickname",
                     "<< \"Updating avatar: \" << *request.avatar",
                 }) {
                EXPECT_EQ(CountOccurrences(update_profile_body, raw_profile_log), 0U)
                    << raw_profile_log;
            }

            for (const auto* preserved_update : {
                     "if (request.nickname.has_value())",
                     "user.setNickname(*request.nickname);",
                     "if (request.avatar.has_value())",
                     "user.setAvatar(*request.avatar);",
                     "co_await mapper.update(user);",
                 }) {
                EXPECT_EQ(CountOccurrences(update_profile_body, preserved_update), 1U)
                    << preserved_update;
            }
            for (const auto* response_mapping : {
                     "response.id = user.getValueOfId();",
                     "response.username = user.getValueOfUsername();",
                     "response.email = user.getValueOfEmail();",
                     "response.nickname = user.getNickname() ? *user.getNickname() : \"\";",
                     "response.avatar = user.getAvatar() ? *user.getAvatar() : \"\";",
                     "response.storage_quota = user.getValueOfStorageQuota();",
                     "response.storage_used = user.getValueOfStorageUsed();",
                     "response.file_count = 0;",
                     "response.folder_count = 0;",
                     "response.created_at = user.getValueOfCreatedAt().toDbStringLocal();",
                     "response.updated_at = user.getValueOfUpdatedAt().toDbStringLocal();",
                 }) {
                EXPECT_EQ(CountOccurrences(update_profile_body, response_mapping), 1U)
                    << response_mapping;
            }
        }

        TEST(UserAccountValueLogContractTest, ReadAndPasswordUseFixedSummaries) {
            const auto service_source = ReadSourceFile("src/services/UserService.cpp");
            const auto get_profile_body = ExtractBetween(
                service_source,
                "auto UserService::GetProfile(",
                "auto UserService::ChangePassword("
            );
            const auto change_password_body = ExtractBetween(
                service_source,
                "auto UserService::ChangePassword(",
                "auto UserService::UpdateProfile("
            );

            ASSERT_FALSE(get_profile_body.empty());
            ASSERT_FALSE(change_password_body.empty());
            EXPECT_EQ(
                CountOccurrences(
                    get_profile_body,
                    "Logger::Debug(log_context) << \"User profile record loaded\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    change_password_body,
                    "Logger::Debug(log_context) << \"Password change target loaded\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    get_profile_body,
                    "<< \"Found user: \" << row[\"username\"].as<std::string>()"
                ),
                0U
            );
            EXPECT_EQ(
                CountOccurrences(
                    change_password_body,
                    "<< \"Found user: \" << user.getValueOfUsername()"
                ),
                0U
            );

            for (const auto* response_mapping : {
                     "response.id = row[\"id\"].as<uint64_t>();",
                     "response.username = row[\"username\"].as<std::string>();",
                     "response.email = row[\"email\"].as<std::string>();",
                     "response.nickname = row[\"nickname\"].as<std::string>();",
                     "response.avatar = row[\"avatar\"].as<std::string>();",
                     "response.storage_quota = row[\"storage_quota\"].as<uint64_t>();",
                     "response.storage_used = row[\"storage_used\"].as<uint64_t>();",
                     "response.file_count = row[\"file_count\"].as<uint32_t>();",
                     "response.folder_count = row[\"folder_count\"].as<uint32_t>();",
                     "response.created_at = row[\"created_at\"].as<std::string>();",
                     "response.updated_at = row[\"updated_at\"].as<std::string>();",
                 }) {
                EXPECT_EQ(CountOccurrences(get_profile_body, response_mapping), 1U)
                    << response_mapping;
            }
            for (const auto* password_step : {
                     "HashUtil::VerifyPassword(request.old_password, user.getValueOfPasswordHash())",
                     "request.old_password == request.new_password",
                     "HashUtil::HashPassword(request.new_password)",
                     "user.setPasswordHash(hash_result.value());",
                     "co_await mapper.update(user);",
                 }) {
                EXPECT_EQ(CountOccurrences(change_password_body, password_step), 1U)
                    << password_step;
            }
        }

    } // namespace
} // namespace disk::user
