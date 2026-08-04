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

        TEST(AuthLoginServiceValueLogContractTest, LoginUsesFixedSummaries) {
            const auto service_source = ReadSourceFile("src/services/AuthService.cpp");
            const auto login_body = ExtractBetween(
                service_source,
                "auto AuthService::Login(",
                "auto AuthService::RefreshTokens("
            );

            ASSERT_FALSE(login_body.empty());
            for (const auto* fixed_log : {
                     "Logger::Debug(log_context) << \"User login started\";",
                     "Logger::Warn(log_context) << \"Login rate limit exceeded\";",
                     "Logger::Warn(log_context) << \"Redis rate limit check failed\";",
                     "Logger::Warn(log_context) << \"Login account not found\";",
                     "Logger::Warn(log_context) << \"Login password rejected\";",
                     "Logger::Warn(log_context) << \"Failed to store refresh token\";",
                     "Logger::Info(log_context) << \"User login successful\";",
                 }) {
                EXPECT_EQ(CountOccurrences(login_body, fixed_log), 1U) << fixed_log;
            }
            for (const auto* raw_login_log : {
                     "\"User login attempt: \" << request.account",
                     "<< \"Login rate limit triggered: ip=\" << client_ip << \", attempts=\" << count",
                     "<< \"User not found: \" << request.account",
                     "<< \"Invalid password: \" << request.account",
                     "<< \"Failed to store refresh_token in Redis: \" << user.getValueOfId()",
                     "<< \"User login successful: \" << request.account",
                 }) {
                EXPECT_EQ(CountOccurrences(login_body, raw_login_log), 0U) << raw_login_log;
            }

            for (const auto* preserved_login_step : {
                     "disk::redis::RedisKeyPrefix::ExtractIPOnly(ip_address)",
                     "disk::redis::RedisKeyPrefix::BuildLoginRateLimitKey(client_ip)",
                     "m_redis_service->IncrWithExpire(rate_key, 300, log_context)",
                     "if (count > 5)",
                     "ErrorCode::TooManyRequests",
                     "FindUser(request.account, log_context)",
                     "ValidateAccountAccess(user.getValueOfId(), log_context)",
                     "co_await RunOnAuthCpuPool(",
                     "HashUtil::VerifyPassword(password, stored_password_hash)",
                     "IncrementLoginAttempts(user.getValueOfId(), log_context)",
                     "UpdateLoginInfo(user.getValueOfId(), client_ip, log_context)",
                     "TokenService::GetInstance()->GenerateTokens(",
                     "TokenService::GetInstance()->StoreRefreshToken(",
                     "response.access_token = access_token;",
                     "response.refresh_token = refresh_token;",
                     "response.token_type = \"Bearer\";",
                     "response.expires_in = disk::services::TokenService::GetAccessTokenExpireSeconds();",
                     "response.user = UserToResponse(user);",
                     "co_return response;",
                 }) {
                EXPECT_EQ(CountOccurrences(login_body, preserved_login_step), 1U)
                    << preserved_login_step;
            }
        }

        TEST(AuthRefreshServiceValueLogContractTest, RefreshUsesFixedSummaries) {
            const auto service_source = ReadSourceFile("src/services/AuthService.cpp");
            const auto refresh_body = ExtractBetween(
                service_source,
                "auto AuthService::RefreshTokens(",
                "AuthService::Logout("
            );

            ASSERT_FALSE(refresh_body.empty());
            for (const auto* fixed_log : {
                     "Logger::Debug(log_context) << \"Starting token refresh\";",
                     "Logger::Warn(log_context) << \"Refresh token verification failed\";",
                     "Logger::Debug(log_context) << \"Refresh token verified\";",
                     "Logger::Debug(log_context) << \"Refresh user loaded\";",
                     "Logger::Warn(log_context) << \"Refresh token renewal failed\";",
                     "Logger::Info(log_context) << \"Token refresh successful\";",
                     "Logger::Error(log_context) << \"User query failed\";",
                     "Logger::Error(log_context) << \"Token refresh processing failed\";",
                 }) {
                EXPECT_EQ(CountOccurrences(refresh_body, fixed_log), 1U) << fixed_log;
            }
            for (const auto* raw_refresh_log : {
                     "<< \"Refresh token verified successfully: user_id=\" << user_id << \", jti=\" << jti",
                     "<< \"Found user: \" << user.getValueOfUsername()",
                     "<< \"Refresh token renewal failed: \" << user.getValueOfId()",
                     "<< \"Token refresh successful: user_id=\" << user_id",
                 }) {
                EXPECT_EQ(CountOccurrences(refresh_body, raw_refresh_log), 0U)
                    << raw_refresh_log;
            }

            for (const auto* preserved_refresh_step : {
                     "TokenService::GetInstance()->VerifyRefreshToken(",
                     "const auto [user_id, jti] = verify_result.value();",
                     "CoroMapper<Users> mapper(m_db_client);",
                     "mapper.findOne(Criteria(Users::Cols::_id, CompareOperator::EQ, user_id))",
                     "ValidateAccountAccess(user_id, log_context)",
                     "TokenService::GetInstance()->GenerateTokens(",
                     "TokenService::GetInstance()->RefreshRefreshToken(",
                     "response.access_token = access_token;",
                     "response.refresh_token = new_refresh_token;",
                     "response.expires_in = disk::services::TokenService::GetAccessTokenExpireSeconds();",
                     "co_return response;",
                     "catch (const drogon::orm::DrogonDbException&)",
                     "ErrorInfo(ErrorCode::UserNotFound)",
                     "catch (const std::exception&)",
                     "ErrorInfo(ErrorCode::InternalError, \"Token refresh failed, please try again later\")",
                 }) {
                EXPECT_EQ(CountOccurrences(refresh_body, preserved_refresh_step), 1U)
                    << preserved_refresh_step;
            }
            EXPECT_EQ(CountOccurrences(refresh_body, "request.refresh_token,"), 2U);
            EXPECT_EQ(CountOccurrences(refresh_body, "new_refresh_token,"), 1U);
        }

        TEST(AuthLogoutServiceValueLogContractTest, LogoutUsesFixedSummaries) {
            const auto service_source = ReadSourceFile("src/services/AuthService.cpp");
            const auto logout_body = ExtractBetween(
                service_source,
                "AuthService::Logout(",
                "auto AuthService::FindUser("
            );

            ASSERT_FALSE(logout_body.empty());
            for (const auto* fixed_log : {
                     "Logger::Info(log_context) << \"User logout started\";",
                     "Logger::Warn(log_context) << \"Access token invalidation failed\";",
                     "Logger::Warn(log_context) << \"Refresh token revocation failed\";",
                     "Logger::Debug(log_context) << \"Logout audit recorded\";",
                     "Logger::Warn(log_context) << \"Failed to record logout log\";",
                     "Logger::Info(log_context) << \"User logout successful\";",
                 }) {
                EXPECT_EQ(CountOccurrences(logout_body, fixed_log), 1U) << fixed_log;
            }
            for (const auto* raw_logout_log : {
                     "<< \"User logout: user_id=\" << user_id << \", ip=\" << client_ip",
                     "<< \"Access token invalidation failed: user_id=\" << user_id",
                     "<< \"Refresh token revocation failed: user_id=\" << user_id",
                     "<< \"Logout log recorded: user_id=\" << user_id",
                     "<< \"User logout successful: user_id=\" << user_id",
                 }) {
                EXPECT_EQ(CountOccurrences(logout_body, raw_logout_log), 0U) << raw_logout_log;
            }

            for (const auto* preserved_logout_step : {
                     "disk::redis::RedisKeyPrefix::ExtractIPOnly(ip_address)",
                     "TokenService::GetInstance()->InvalidateAccessToken(",
                     "if (!invalidate_result.has_value())",
                     "ErrorInfo(ErrorCode::InternalError, \"Logout failed, please try again later\")",
                     "TokenService::GetInstance()->RevokeRefreshToken(user_id, log_context)",
                     "if (!revoke_result)",
                     "CoroMapper<drogon_model::disk::OperationLogs> mapper(m_db_client)",
                     "drogon_model::disk::OperationLogs log;",
                     "log.setUserId(user_id);",
                     "log.setAction(\"logout\");",
                     "log.setTargetType(\"user\");",
                     "log.setTargetId(0);",
                     "details_json[\"message\"] = \"User logged out\";",
                     "builder[\"indentation\"] = \"\";",
                     "log.setDetails(Json::writeString(builder, details_json));",
                     "log.setIpAddress(client_ip);",
                     "log.setCreatedAt(trantor::Date::now());",
                     "co_await mapper.insert(log);",
                     "catch (const drogon::orm::DrogonDbException&)",
                     "co_return {};",
                 }) {
                EXPECT_EQ(CountOccurrences(logout_body, preserved_logout_step), 1U)
                    << preserved_logout_step;
            }
            EXPECT_EQ(CountOccurrences(logout_body, "access_token,"), 2U);
        }

        TEST(AuthFindUserValueLogContractTest, LookupUsesFixedSummaries) {
            const auto service_source = ReadSourceFile("src/services/AuthService.cpp");
            const auto find_user_body = ExtractBetween(
                service_source,
                "auto AuthService::FindUser(",
                "auto AuthService::ValidateAccountAccess("
            );

            ASSERT_FALSE(find_user_body.empty());
            for (const auto* fixed_log : {
                     "Logger::Warn(log_context) << \"User lookup found no match\";",
                     "Logger::Debug(log_context) << \"User lookup succeeded\";",
                     "Logger::Warn(log_context) << \"User lookup failed\";",
                 }) {
                EXPECT_EQ(CountOccurrences(find_user_body, fixed_log), 1U) << fixed_log;
            }
            for (const auto* raw_lookup_log : {
                     "<< \"User not found: \" << account",
                     "<< \"Found user: \" << account",
                 }) {
                EXPECT_EQ(CountOccurrences(find_user_body, raw_lookup_log), 0U)
                    << raw_lookup_log;
            }

            for (const auto* preserved_lookup_step : {
                     "m_db_client->execSqlCoro(",
                     "SELECT id, username, email, password_hash, nickname, avatar, ",
                     "storage_quota, storage_used, storage_reserved, status, role, ",
                     "login_attempts, locked_until, last_login_at, last_login_ip, ",
                     "created_at, updated_at ",
                     "FROM users WHERE username = $1 OR email = $1 LIMIT 1",
                     "if (result.empty())",
                     "Users user(result[0], -1);",
                     "co_return user;",
                     "catch (const drogon::orm::DrogonDbException&)",
                 }) {
                EXPECT_EQ(CountOccurrences(find_user_body, preserved_lookup_step), 1U)
                    << preserved_lookup_step;
            }
            EXPECT_EQ(
                CountOccurrences(
                    find_user_body,
                    "co_return std::unexpected(ErrorInfo(ErrorCode::UserNotFound));"
                ),
                2U
            );
        }

        TEST(AuthAccountAccessValueLogContractTest, ValidationUsesFixedSummaries) {
            const auto service_source = ReadSourceFile("src/services/AuthService.cpp");
            const auto validation_body = ExtractBetween(
                service_source,
                "auto AuthService::ValidateAccountAccess(",
                "auto AuthService::UpdateLoginInfo("
            );

            ASSERT_FALSE(validation_body.empty());
            for (const auto* fixed_log : {
                     "Logger::Warn(log_context) << \"Account access disabled\";",
                     "Logger::Warn(log_context) << \"Account access locked\";",
                     "Logger::Error(log_context) << \"Failed to validate account access\";",
                 }) {
                EXPECT_EQ(CountOccurrences(validation_body, fixed_log), 1U) << fixed_log;
            }
            for (const auto* raw_account_log : {
                     "<< \"Account disabled: user_id=\" << user_id",
                     "<< \"Account locked: user_id=\" << user_id",
                 }) {
                EXPECT_EQ(CountOccurrences(validation_body, raw_account_log), 0U)
                    << raw_account_log;
            }

            for (const auto* preserved_validation_step : {
                     "m_db_client->execSqlCoro(",
                     "SELECT status, ((status = 2 AND locked_until IS NULL)",
                     "OR COALESCE(locked_until > NOW(), FALSE)) AS account_locked",
                     "FROM users WHERE id = $1",
                     "if (result.empty())",
                     "ErrorInfo(ErrorCode::UserNotFound)",
                     "result[0][\"status\"].as<int>() == ACCOUNT_STATUS_DISABLED",
                     "ErrorInfo(ErrorCode::AccountDisabled)",
                     "result[0][\"account_locked\"].as<bool>()",
                     "ErrorInfo(ErrorCode::AccountLocked)",
                     "co_return {};",
                     "catch (const drogon::orm::DrogonDbException&)",
                     "ErrorInfo(ErrorCode::InternalError, \"Failed to validate account status\")",
                 }) {
                EXPECT_EQ(CountOccurrences(validation_body, preserved_validation_step), 1U)
                    << preserved_validation_step;
            }
        }

        TEST(AuthLoginStateUpdateValueLogContractTest, UpdateUsesFixedSummaries) {
            const auto service_source = ReadSourceFile("src/services/AuthService.cpp");
            const auto update_body = ExtractBetween(
                service_source,
                "auto AuthService::UpdateLoginInfo(",
                "auto AuthService::IncrementLoginAttempts("
            );

            ASSERT_FALSE(update_body.empty());
            for (const auto* fixed_log : {
                     "Logger::Error(log_context) << \"Login state changed before update\";",
                     "Logger::Debug(log_context) << \"Login info updated\";",
                     "Logger::Debug(log_context) << \"Login rate limit counter cleared\";",
                     "Logger::Warn(log_context) << \"Failed to clear login rate limit counter\";",
                     "Logger::Error(log_context) << \"Failed to update login info\";",
                 }) {
                EXPECT_EQ(CountOccurrences(update_body, fixed_log), 1U) << fixed_log;
            }
            for (const auto* raw_update_log : {
                     "<< \"Login state changed before update: user_id=\" << user_id",
                     "<< \"Login info updated successfully: \" << user_id",
                     "<< \"Login rate limit counter cleared: ip=\" << client_ip",
                 }) {
                EXPECT_EQ(CountOccurrences(update_body, raw_update_log), 0U) << raw_update_log;
            }

            for (const auto* preserved_update_step : {
                     "m_db_client->execSqlCoro(",
                     "UPDATE users SET status = $1, login_attempts = 0, locked_until = NULL",
                     "last_login_at = NOW(), last_login_ip = $2, updated_at = NOW()",
                     "WHERE id = $3 AND ((status = $1 AND",
                     "locked_until IS NULL OR locked_until <= NOW()",
                     "status = $4 AND locked_until IS NOT NULL AND locked_until <= NOW()",
                     "RETURNING id",
                     "ACCOUNT_STATUS_ACTIVE",
                     "ACCOUNT_STATUS_LOCKED",
                     "if (result.empty())",
                     "ValidateAccountAccess(user_id, log_context)",
                     "co_return std::unexpected(access_result.error());",
                     "disk::redis::RedisKeyPrefix::BuildLoginRateLimitKey(client_ip)",
                     "m_redis_service->Delete(rate_key, log_context)",
                     "if (delete_result.has_value())",
                     "catch (const drogon::orm::DrogonDbException&)",
                     "co_return {};",
                 }) {
                EXPECT_EQ(CountOccurrences(update_body, preserved_update_step), 1U)
                    << preserved_update_step;
            }
            EXPECT_EQ(
                CountOccurrences(
                    update_body,
                    "ErrorInfo(ErrorCode::InternalError, \"Failed to update login state\")"
                ),
                2U
            );
        }

        TEST(AuthLoginFailureCounterValueLogContractTest, IncrementUsesFixedSummaries) {
            const auto service_source = ReadSourceFile("src/services/AuthService.cpp");
            const auto increment_body = ExtractBetween(
                service_source,
                "auto AuthService::IncrementLoginAttempts(",
                "} // namespace disk::auth"
            );

            ASSERT_FALSE(increment_body.empty());
            for (const auto* fixed_log : {
                     "Logger::Debug(log_context) << \"Login attempt not counted for unavailable account\";",
                     "Logger::Warn(log_context) << \"Login failure threshold reached\";",
                     "Logger::Warn(log_context) << \"Login failure recorded\";",
                     "Logger::Error(log_context) << \"Failed to increment login attempts\";",
                 }) {
                EXPECT_EQ(CountOccurrences(increment_body, fixed_log), 1U) << fixed_log;
            }
            for (const auto* raw_counter_log : {
                     "<< \"Login attempt not counted for unavailable account: user_id=\" << user_id",
                     "<< \"Account locked: \" << user_id",
                     "<< \"Failed login attempts: \" << user_id",
                 }) {
                EXPECT_EQ(CountOccurrences(increment_body, raw_counter_log), 0U)
                    << raw_counter_log;
            }
            EXPECT_EQ(
                CountOccurrences(increment_body, "result[0][\"login_attempts\"].as<int>()"),
                0U
            );

            for (const auto* preserved_counter_step : {
                     "m_db_client->execSqlCoro(",
                     "UPDATE users SET status = $1,",
                     "login_attempts = CASE",
                     "locked_until = CASE WHEN (CASE",
                     ") >= $2",
                     "THEN NOW() + INTERVAL '15 minutes' ELSE NULL END, updated_at = NOW()",
                     "WHERE id = $3 AND ((status = $1 AND",
                     "(locked_until IS NULL OR locked_until <= NOW())) OR",
                     "(status = $4 AND locked_until IS NOT NULL AND locked_until <= NOW()))",
                     "RETURNING login_attempts, locked_until > NOW() AS account_locked",
                     "ACCOUNT_STATUS_ACTIVE",
                     "LOGIN_FAILURE_LIMIT",
                     "ACCOUNT_STATUS_LOCKED",
                     "if (result.empty())",
                     "result[0][\"account_locked\"].as<bool>()",
                     "catch (const drogon::orm::DrogonDbException&)",
                     "ErrorInfo(ErrorCode::InternalError, \"Failed to record login attempt\")",
                 }) {
                EXPECT_EQ(CountOccurrences(increment_body, preserved_counter_step), 1U)
                    << preserved_counter_step;
            }
            EXPECT_EQ(
                CountOccurrences(
                    increment_body,
                    "WHEN locked_until IS NOT NULL AND locked_until <= NOW() THEN 1"
                ),
                2U
            );
            EXPECT_EQ(
                CountOccurrences(increment_body, "ELSE login_attempts + 1 END"),
                2U
            );
            EXPECT_EQ(CountOccurrences(increment_body, "co_return {};"), 2U);
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

        TEST(AuthRegistrationControllerValueLogContractTest, RegisterUsesFixedSummaries) {
            const auto controller_source = ReadSourceFile("src/controllers/AuthController.cpp");
            const auto register_controller = ExtractBetween(
                controller_source,
                "auto AuthController::Register(",
                "auto AuthController::Login("
            );

            ASSERT_FALSE(register_controller.empty());
            for (const auto* fixed_log : {
                     "Logger::Info(log_context) << \"Received user registration request\";",
                     "Logger::Warn(log_context) << \"User registration request validation failed\";",
                     "Logger::Debug(log_context) << \"User registration parameters validated\";",
                     "Logger::Error(log_context) << \"User registration business logic failed\";",
                     "Logger::Info(log_context) << \"User registration successful\";",
                 }) {
                EXPECT_EQ(CountOccurrences(register_controller, fixed_log), 1U) << fixed_log;
            }
            for (const auto* raw_boundary_log : {
                     "<< \"Received user registration request: \" << request->getPeerAddr().toIpPort()",
                     "<< \"User registration request validation failed: \"",
                     "<< \"User registration parameters validated: \" << parse_result->username",
                     "<< \"User registration business logic failed: \"",
                     "<< \"User registration successful: \" << register_result->username",
                 }) {
                EXPECT_EQ(CountOccurrences(register_controller, raw_boundary_log), 0U)
                    << raw_boundary_log;
            }

            for (const auto* preserved_controller_step : {
                     "GetRequestLogContext(request, \"auth\")",
                     "RegisterRequest::FromRequest(request, log_context)",
                     "co_return Response::Error(parse_result.error());",
                     "m_auth_service->Register(*parse_result, log_context)",
                     "co_return Response::Error(register_result.error());",
                     "data[\"user\"] = register_result->ToJson();",
                     "co_return Response::Success(data);",
                 }) {
                EXPECT_EQ(CountOccurrences(register_controller, preserved_controller_step), 1U)
                    << preserved_controller_step;
            }
        }

        TEST(AuthLoginControllerValueLogContractTest, LoginUsesFixedSummaries) {
            const auto controller_source = ReadSourceFile("src/controllers/AuthController.cpp");
            const auto login_controller = ExtractBetween(
                controller_source,
                "auto AuthController::Login(",
                "auto AuthController::RefreshTokens("
            );

            ASSERT_FALSE(login_controller.empty());
            for (const auto* fixed_log : {
                     "Logger::Info(log_context) << \"Received login request\";",
                     "Logger::Warn(log_context) << \"Login request validation failed\";",
                     "Logger::Error(log_context) << \"Login failed\";",
                     "Logger::Info(log_context) << \"Login successful\";",
                 }) {
                EXPECT_EQ(CountOccurrences(login_controller, fixed_log), 1U) << fixed_log;
            }
            for (const auto* raw_boundary_log : {
                     "<< \"Received login request: \" << request->getPeerAddr().toIpPort()",
                     "<< \"Login request validation failed: \" << parse_result.error().message",
                     "<< \"Login failed: \" << login_result.error().message",
                     "<< \"Login successful: \" << parse_result->account",
                 }) {
                EXPECT_EQ(CountOccurrences(login_controller, raw_boundary_log), 0U)
                    << raw_boundary_log;
            }

            for (const auto* preserved_controller_step : {
                     "GetRequestLogContext(request, \"auth\")",
                     "LoginRequest::FromRequest(request, log_context)",
                     "co_return Response::Error(parse_result.error());",
                     "disk::utils::ResolveClientIp(request)",
                     "m_auth_service->Login(*parse_result, ip_address, log_context)",
                     "co_return Response::Error(login_result.error());",
                     "co_return Response::Success(login_result->ToJson());",
                 }) {
                EXPECT_EQ(CountOccurrences(login_controller, preserved_controller_step), 1U)
                    << preserved_controller_step;
            }
        }

        TEST(AuthRefreshControllerValueLogContractTest, RefreshUsesFixedSummaries) {
            const auto controller_source = ReadSourceFile("src/controllers/AuthController.cpp");
            const auto refresh_controller = ExtractBetween(
                controller_source,
                "auto AuthController::RefreshTokens(",
                "auto AuthController::Logout("
            );

            ASSERT_FALSE(refresh_controller.empty());
            for (const auto* fixed_log : {
                     "Logger::Info(log_context) << \"Received refresh token request\";",
                     "Logger::Warn(log_context) << \"Refresh token request validation failed\";",
                     "Logger::Error(log_context) << \"Refresh token failed\";",
                     "Logger::Info(log_context) << \"Refresh token successful\";",
                 }) {
                EXPECT_EQ(CountOccurrences(refresh_controller, fixed_log), 1U) << fixed_log;
            }
            for (const auto* raw_boundary_log : {
                     "<< \"Received refresh token request: \" << request->getPeerAddr().toIpPort()",
                     "<< \"Refresh token request validation failed: \" << parse_result.error().message",
                     "<< \"Refresh token failed: \" << refresh_result.error().message",
                 }) {
                EXPECT_EQ(CountOccurrences(refresh_controller, raw_boundary_log), 0U)
                    << raw_boundary_log;
            }

            for (const auto* preserved_controller_step : {
                     "GetRequestLogContext(request, \"auth\")",
                     "RefreshTokenRequest::FromRequest(request, log_context)",
                     "co_return Response::Error(parse_result.error());",
                     "m_auth_service->RefreshTokens(*parse_result, log_context)",
                     "co_return Response::Error(refresh_result.error());",
                     "co_return Response::Success(refresh_result->ToJson());",
                 }) {
                EXPECT_EQ(CountOccurrences(refresh_controller, preserved_controller_step), 1U)
                    << preserved_controller_step;
            }
        }

        TEST(AuthLogoutControllerValueLogContractTest, LogoutUsesFixedSummaries) {
            const auto controller_source = ReadSourceFile("src/controllers/AuthController.cpp");
            const auto logout_controller =
                ExtractFrom(controller_source, "auto AuthController::Logout(");

            ASSERT_FALSE(logout_controller.empty());
            for (const auto* fixed_log : {
                     "<< \"Received logout request\";",
                     "<< \"Logout request missing Authorization header\";",
                     "<< \"Logout request Authorization header format invalid\";",
                     "<< \"Logout request missing user_id attribute\";",
                     "<< \"Logout failed\";",
                     "<< \"Logout successful\";",
                 }) {
                EXPECT_EQ(CountOccurrences(logout_controller, fixed_log), 1U) << fixed_log;
            }
            EXPECT_EQ(CountOccurrences(logout_controller, "Logger::Info(log_context)"), 2U);
            EXPECT_EQ(CountOccurrences(logout_controller, "Logger::Warn(log_context)"), 3U);
            EXPECT_EQ(CountOccurrences(logout_controller, "Logger::Error(log_context)"), 1U);

            for (const auto* raw_boundary_log : {
                     "<< \"Received logout request: \" << request->getPeerAddr().toIpPort()",
                     "<< \"Logout failed: \" << logout_result.error().message",
                     "<< \"Logout successful: user_id=\" << user_id",
                 }) {
                EXPECT_EQ(CountOccurrences(logout_controller, raw_boundary_log), 0U)
                    << raw_boundary_log;
            }

            for (const auto* preserved_controller_step : {
                     "GetRequestLogContext(request, \"auth\")",
                     "request->getHeader(\"Authorization\")",
                     "if (auth_header.empty())",
                     "ErrorInfo(ErrorCode::TokenMissing)",
                     "if (!auth_header.starts_with(\"Bearer \"))",
                     "ErrorInfo(ErrorCode::TokenMalformed)",
                     "auth_header.substr(7)",
                     "request->attributes()->find(\"user_id\")",
                     "ErrorInfo(ErrorCode::InvalidToken)",
                     "request->attributes()->get<uint64_t>(\"user_id\")",
                     "disk::utils::ResolveClientIp(request)",
                     "m_auth_service->Logout(user_id, access_token, ip_address, log_context)",
                     "co_return Response::Error(logout_result.error());",
                     "co_return Response::Success({});",
                 }) {
                EXPECT_EQ(CountOccurrences(logout_controller, preserved_controller_step), 1U)
                    << preserved_controller_step;
            }
        }

    } // namespace
} // namespace disk::auth
