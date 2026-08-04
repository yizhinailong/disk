/**
 * @file TokenServiceLogContext_test.cpp
 * @brief Token service request correlation contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>
#include <json/json.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "services/TokenService.hpp"
#include "utils/LogHelper.hpp"

namespace disk::services {
    namespace {

        constexpr std::string_view TEST_JWT_SECRET =
            "token-context-test-secret-key-32-bytes";
        constexpr std::string_view RAW_ACCESS_TOKEN =
            "raw-token-service-access-value";
        constexpr std::string_view RAW_REFRESH_TOKEN =
            "raw-token-service-refresh-value";
        constexpr std::string_view RAW_REVOKE_TOKEN =
            "raw-token-service-revoke-value";
        constexpr std::string_view RAW_SHARE_TOKEN =
            "raw-token-service-share-value";

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

        auto SourceSection(
            const std::string& source,
            std::string_view begin_marker,
            std::string_view end_marker
        ) -> std::string {
            const auto begin = source.find(begin_marker);
            const auto end = source.find(end_marker, begin);
            if (begin == std::string::npos || end == std::string::npos || end <= begin) {
                return {};
            }
            return source.substr(begin, end - begin);
        }

        auto StreamsIdentifier(const std::string& source, std::string_view identifier) -> bool {
            size_t position = 0;
            while ((position = source.find("<<", position)) != std::string::npos) {
                auto cursor = position + 2;
                while (cursor < source.size() &&
                       std::isspace(static_cast<unsigned char>(source[cursor])) != 0) {
                    ++cursor;
                }
                const auto end = cursor + identifier.size();
                if (end <= source.size() && source.compare(cursor, identifier.size(), identifier) == 0 &&
                    (end == source.size() ||
                     (std::isalnum(static_cast<unsigned char>(source[end])) == 0 &&
                      source[end] != '_'))) {
                    return true;
                }
                position = cursor;
            }
            return false;
        }

        auto EveryCallContainsContext(
            const std::string& source,
            std::string_view marker,
            size_t expected_count
        ) -> bool {
            size_t count = 0;
            size_t position = 0;
            while ((position = source.find(marker, position)) != std::string::npos) {
                const auto end = source.find(");", position);
                if (end == std::string::npos ||
                    !Contains(source.substr(position, end - position), "log_context")) {
                    return false;
                }
                ++count;
                position = end + 2;
            }
            return count == expected_count;
        }

        auto MakeContext(std::string request_id, std::string operation)
            -> disk::utils::LogContext {
            return {
                .request_id = std::move(request_id),
                .operation = std::move(operation),
            };
        }

        auto Serialize(const Json::Value& value) -> std::string {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            return Json::writeString(builder, value);
        }

        TEST(TokenImplementationHelperContractTest, VerifierBuildersHaveInternalLinkage) {
            const auto header = ReadSourceFile("src/services/TokenService.hpp");
            const auto source = ReadSourceFile("src/services/TokenService.cpp");
            const auto anonymous_helpers = SourceSection(
                source,
                "    namespace {",
                "    } // namespace"
            );

            EXPECT_TRUE(Contains(
                header,
                "using JwtTraits = jwt::traits::open_source_parsers_jsoncpp;"
            ));
            EXPECT_TRUE(Contains(
                header,
                "using JwtVerifier = jwt::verifier<jwt::default_clock, JwtTraits>;"
            ));
            EXPECT_FALSE(Contains(header, "BuildJwtVerifier("));
            EXPECT_FALSE(Contains(header, "BuildShareJwtVerifier("));
            EXPECT_FALSE(Contains(source, "TokenService::BuildJwtVerifier("));
            EXPECT_FALSE(Contains(source, "TokenService::BuildShareJwtVerifier("));
            ASSERT_FALSE(anonymous_helpers.empty());
            EXPECT_TRUE(Contains(
                anonymous_helpers,
                "using JwtTraits = jwt::traits::open_source_parsers_jsoncpp;"
            ));
            EXPECT_TRUE(Contains(
                anonymous_helpers,
                "using JwtVerifier = jwt::verifier<jwt::default_clock, JwtTraits>;"
            ));
            EXPECT_TRUE(Contains(
                anonymous_helpers,
                "[[nodiscard]] auto BuildJwtVerifier(const std::string& jwt_secret) -> JwtVerifier"
            ));
            EXPECT_TRUE(Contains(
                anonymous_helpers,
                "[[nodiscard]] auto BuildShareJwtVerifier(const std::string& jwt_secret) -> JwtVerifier"
            ));
            EXPECT_TRUE(Contains(
                anonymous_helpers,
                "[[nodiscard]] auto IsTokenExpired(\n" "            const jwt::error::token_verification_exception& error\n" "        ) noexcept -> bool"
            ));
            EXPECT_TRUE(Contains(
                anonymous_helpers,
                "error.code() == jwt::error::token_verification_error::token_expired"
            ));
            EXPECT_EQ(CountOccurrences(source, "BuildJwtVerifier("), 3U);
            EXPECT_EQ(CountOccurrences(source, "BuildShareJwtVerifier("), 4U);
            EXPECT_EQ(CountOccurrences(source, "IsTokenExpired("), 4U);
            EXPECT_EQ(
                CountOccurrences(
                    anonymous_helpers,
                    ".allow_algorithm(jwt::algorithm::hs256{ jwt_secret })"
                ),
                2U
            );
            EXPECT_TRUE(Contains(anonymous_helpers, ".with_issuer(\"disk\")"));
            EXPECT_TRUE(Contains(anonymous_helpers, ".with_issuer(\"disk_share\")"));
            EXPECT_TRUE(Contains(
                source,
                "instance->m_jwt_verifier = BuildJwtVerifier(instance->m_jwt_secret)"
            ));
            EXPECT_TRUE(Contains(
                source,
                "BuildShareJwtVerifier(jwt_secret).verify(decoded)"
            ));
        }

        TEST(TokenServiceLogContextContractTest, RequestAndProcessEventsHaveExplicitOwnership) {
            const auto header = ReadSourceFile("src/services/TokenService.hpp");
            const auto source = ReadSourceFile("src/services/TokenService.cpp");
            const auto auth_cpu_pool = ReadSourceFile("src/utils/AuthCpuPool.hpp");
            const auto main_source = ReadSourceFile("src/main.cpp");
            const auto auth_service = ReadSourceFile("src/services/AuthService.cpp");
            const auto jwt_filter = ReadSourceFile("src/filters/JwtAuthFilter.cpp");
            const auto share_filter = ReadSourceFile("src/filters/ShareAuthFilter.cpp");
            const auto share_service = ReadSourceFile("src/services/ShareService.cpp");

            ASSERT_FALSE(header.empty());
            ASSERT_FALSE(source.empty());
            EXPECT_TRUE(Contains(header, "auto GetAuthCpuWorkLoop()"));
            EXPECT_FALSE(Contains(header, "StartAuthCpuPoolMetricsTimer"));
            EXPECT_FALSE(Contains(header, "GetAuthCpuPoolActiveTaskCount"));
            EXPECT_FALSE(Contains(source, "auto StartAuthCpuPoolMetricsTimer("));
            EXPECT_FALSE(Contains(source, "auto GetAuthCpuPoolActiveTaskCount("));
            EXPECT_TRUE(Contains(auth_cpu_pool, "detail::GetAuthCpuWorkLoop()"));
            EXPECT_TRUE(Contains(
                main_source,
                "TokenService::GetInstance()->StartCacheMaintenance();"
            ));
            EXPECT_TRUE(Contains(source, "StartPoolMetricsTimer(metrics_interval);"));
            EXPECT_TRUE(Contains(source, "[this]() { LogPoolMetrics(); }"));
            EXPECT_EQ(
                CountOccurrences(header, "disk::utils::LogContext log_context = {}"),
                12U
            );
            EXPECT_FALSE(Contains(header, "auto RevokeShareToken("));
            EXPECT_FALSE(Contains(source, "TokenService::RevokeShareToken("));
            EXPECT_FALSE(Contains(header, "IsRevocationCacheEntryRevokedForTest"));
            EXPECT_FALSE(Contains(source, "IsRevocationCacheEntryRevokedForTest"));
            EXPECT_EQ(CountOccurrences(source, "Logger::Debug(log_context)"), 4U);
            EXPECT_EQ(CountOccurrences(source, "Logger::Info(log_context)"), 2U);
            EXPECT_EQ(CountOccurrences(source, "Logger::Trace(log_context)"), 2U);
            EXPECT_EQ(CountOccurrences(source, "Logger::Warn(log_context)"), 11U);
            EXPECT_EQ(CountOccurrences(source, "Logger::Error(log_context)"), 2U);
            EXPECT_EQ(CountOccurrences(source, ".what()"), 0U);
            EXPECT_FALSE(Contains(source, ".find(\"expired\")"));
            EXPECT_TRUE(Contains(
                source,
                "Logger::Warn(log_context) << \"JWT verification failed\";"
            ));
            EXPECT_TRUE(Contains(
                source,
                "Logger::Warn(log_context) << \"Refresh token verification failed\";"
            ));
            EXPECT_TRUE(Contains(
                source,
                "Logger::Warn(log_context) << \"Share token verification failed\";"
            ));
            EXPECT_TRUE(Contains(
                source,
                "Logger::Error(log_context) << \"Failed to generate share token\";"
            ));

            EXPECT_EQ(
                CountOccurrences(source, "Logger::Debug(AuthRuntimeLogContext())"),
                3U
            );
            EXPECT_EQ(
                CountOccurrences(source, "Logger::Info(AuthRuntimeLogContext())"),
                3U
            );
            EXPECT_EQ(CountOccurrences(source, ".operation = \"auth_runtime\""), 1U);
            EXPECT_FALSE(Contains(source, "Logger::Debug()"));
            EXPECT_FALSE(Contains(source, "Logger::Info()"));
            EXPECT_FALSE(Contains(source, "Logger::Trace()"));
            EXPECT_FALSE(Contains(source, "Logger::Warn()"));
            EXPECT_FALSE(Contains(source, "Logger::Error()"));
            EXPECT_TRUE(Contains(source, "Auth CPU pool initialized: threads="));
            EXPECT_TRUE(Contains(source, "Token service constructed"));
            EXPECT_TRUE(Contains(
                source,
                "Revocation cache maintenance started: interval_seconds="
            ));
            EXPECT_TRUE(Contains(
                source,
                "Auth CPU pool metrics started: interval_seconds="
            ));
            EXPECT_TRUE(Contains(source, "Auth CPU pool metrics: period_seconds="));
            EXPECT_TRUE(Contains(
                source,
                "Token cache eviction completed: access_evicted="
            ));
            EXPECT_FALSE(Contains(source, "instance_id="));
            EXPECT_FALSE(Contains(source, "CalculateRemainingTtl"));
            EXPECT_FALSE(Contains(header, "CalculateRemainingTtl"));
            EXPECT_FALSE(Contains(header, "m_redis_client"));

            EXPECT_TRUE(EveryCallContainsContext(
                auth_service,
                "TokenService::GetInstance()->GenerateTokens(",
                2U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                auth_service,
                "TokenService::GetInstance()->VerifyRefreshToken(",
                1U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                auth_service,
                "TokenService::GetInstance()->RefreshRefreshToken(",
                1U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                auth_service,
                "TokenService::GetInstance()->InvalidateAccessToken(",
                1U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                auth_service,
                "TokenService::GetInstance()->StoreRefreshToken(",
                1U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                auth_service,
                "TokenService::GetInstance()->RevokeRefreshToken(",
                1U
            ));
            EXPECT_TRUE(Contains(jwt_filter, "[token_service, token, log_context]"));
            EXPECT_TRUE(EveryCallContainsContext(
                jwt_filter,
                "token_service->VerifyAccessToken(",
                1U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                jwt_filter,
                "token_service->IsAccessTokenRevoked(",
                1U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                share_filter,
                "TokenService::GetInstance()->VerifyShareTokenWithRedis(",
                1U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                share_service,
                "TokenService::GenerateShareToken(",
                1U
            ));
            EXPECT_TRUE(Contains(source, "VerifyShareToken(m_jwt_secret, token, log_context)"));
            EXPECT_TRUE(Contains(source, "ExtractJti(token, log_context)"));
            EXPECT_TRUE(Contains(source, "IsShareTokenRevoked(hash_result.value(), log_context)"));
            EXPECT_TRUE(Contains(source, "REFRESH_TOKEN_TTL, log_context"));
            EXPECT_TRUE(Contains(source, "ACCESS_TOKEN_TTL, log_context"));
            EXPECT_FALSE(Contains(source, "SHARE_TOKEN_TTL, log_context"));
            EXPECT_TRUE(Contains(
                source,
                "RedisKeyPrefix::BuildShareTokenBlacklistKey(token_hash)"
            ));
            EXPECT_TRUE(Contains(source, "m_redis_service->Delete(key, log_context)"));
            EXPECT_EQ(CountOccurrences(source, "m_redis_service->Exists(key, log_context)"), 2U);

            for (const auto* sensitive : { "token", "old_token", "new_token", "jwt_secret" }) {
                EXPECT_FALSE(StreamsIdentifier(source, sensitive)) << sensitive;
            }
            for (const auto* forbidden : {
                     "log_context.upload_id",
                     "log_context.job_id",
                     "log_context.lease_owner",
                     "log_context.state_version",
                 }) {
                EXPECT_FALSE(Contains(source, forbidden)) << forbidden;
            }
        }

        TEST(TokenRefreshRotationValueLogContractTest, CasUsesFixedSummaries) {
            const auto source = ReadSourceFile("src/services/TokenService.cpp");
            const auto rotation_body = SourceSection(
                source,
                "auto TokenService::RefreshRefreshToken(",
                "auto TokenService::InvalidateAccessToken("
            );

            ASSERT_FALSE(rotation_body.empty());
            for (const auto* fixed_log : {
                     "Logger::Error(log_context) << \"Redis CAS operation failed\";",
                     "Logger::Warn(log_context) << \"Refresh token already used or refreshed\";",
                     "Logger::Debug(log_context) << \"Refresh token rotated successfully\";",
                 }) {
                EXPECT_EQ(CountOccurrences(rotation_body, fixed_log), 1U) << fixed_log;
            }
            for (const auto* raw_rotation_log : {
                     "<< \"Redis CAS operation failed: user_id=\" << user_id",
                     "<< \"Refresh token already used or refreshed (CAS failed): user_id=\" << user_id",
                     "<< \"Refresh token rotated successfully: user_id=\" << user_id",
                 }) {
                EXPECT_EQ(CountOccurrences(rotation_body, raw_rotation_log), 0U)
                    << raw_rotation_log;
            }

            for (const auto* preserved_rotation_step : {
                     "disk::redis::RedisKeyPrefix::BuildRefreshTokenKey(user_id)",
                     "disk::utils::HashUtil::HashToken(old_token)",
                     "if (!old_hash_result)",
                     "co_return std::unexpected(old_hash_result.error());",
                     "disk::utils::HashUtil::HashToken(new_token)",
                     "if (!new_hash_result)",
                     "co_return std::unexpected(new_hash_result.error());",
                     "disk::utils::HashUtil::TokenHashToHex(old_hash_result.value())",
                     "disk::utils::HashUtil::TokenHashToHex(new_hash_result.value())",
                     "m_redis_service->CompareAndSwap(",
                     "old_hash,",
                     "new_hash,",
                     "REFRESH_TOKEN_TTL,",
                     "if (!cas_result)",
                     "co_return std::unexpected(cas_result.error());",
                     "if (!cas_result.value())",
                     "ErrorInfo(disk::error::Code::RefreshTokenAlreadyUsed)",
                     "co_return {};",
                 }) {
                EXPECT_EQ(CountOccurrences(rotation_body, preserved_rotation_step), 1U)
                    << preserved_rotation_step;
            }
            EXPECT_EQ(CountOccurrences(rotation_body, "log_context"), 5U);
        }

        TEST(TokenShareGenerationValueLogContractTest, SuccessUsesFixedSummary) {
            const auto source = ReadSourceFile("src/services/TokenService.cpp");
            const auto generation_body = SourceSection(
                source,
                "auto TokenService::GenerateShareToken(",
                "auto TokenService::VerifyShareToken("
            );

            ASSERT_FALSE(generation_body.empty());
            EXPECT_EQ(
                CountOccurrences(
                    generation_body,
                    "Logger::Debug(log_context) << \"Share token generated successfully\";"
                ),
                1U
            );
            EXPECT_EQ(CountOccurrences(generation_body, "Logger::Debug(log_context)"), 1U);
            for (const auto* raw_share_log : {
                     "<< \"Generated share token: share_code=\" << share_code",
                     "<< \", share_id=\" << share_id",
                 }) {
                EXPECT_EQ(CountOccurrences(generation_body, raw_share_log), 0U)
                    << raw_share_log;
            }

            for (const auto* preserved_generation_step : {
                     "if (!IsValidSharePermission(permission))",
                     "ErrorInfo(disk::error::Code::InvalidParameter, \"Invalid share permission\")",
                     "const auto jti = drogon::utils::getUuid();",
                     "Json::Value scope(Json::objectValue);",
                     "scope[\"share_id\"] = share_code;",
                     "scope[\"permission\"] = permission;",
                     ".set_issuer(\"disk_share\")",
                     ".set_type(\"JWT\")",
                     ".set_subject(std::to_string(share_id))",
                     ".set_payload_claim(\"share_code\", share_code)",
                     ".set_payload_claim(\"type\", \"share\")",
                     ".set_payload_claim(\"jti\", jti)",
                     ".set_payload_claim(\"scope\", scope)",
                     ".set_issued_at(now)",
                     ".set_expires_at(now + std::chrono::seconds(GetShareTokenExpireSeconds()))",
                     ".sign(jwt::algorithm::hs256{ jwt_secret });",
                     "return token;",
                     "catch (const std::exception&)",
                     "Logger::Error(log_context) << \"Failed to generate share token\";",
                     "ErrorInfo(disk::error::Code::InternalError, \"Token generation failed\")",
                 }) {
                EXPECT_EQ(CountOccurrences(generation_body, preserved_generation_step), 1U)
                    << preserved_generation_step;
            }
        }

        class TokenServiceLogContextTest : public ::testing::Test {
        protected:
            auto SetUp() -> void override {
                TokenService::Initialize(std::string(TEST_JWT_SECRET));
                m_service = TokenService::GetInstance();

                m_previous_logger = spdlog::default_logger();
                m_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(m_output);
                m_logger = std::make_shared<spdlog::logger>(
                    "token-service-context-test",
                    m_sink
                );
                m_logger->set_level(spdlog::level::trace);
                disk::utils::Logger::ApplyStructuredFormatter(m_logger);
                spdlog::set_default_logger(m_logger);
                disk::utils::Logger::SetInstanceId("token-service-instance");
            }

            auto TearDown() -> void override {
                disk::utils::Logger::SetInstanceId("");
                spdlog::set_default_logger(m_previous_logger);
            }

            [[nodiscard]] auto DrainRecords(size_t expected_count) -> std::vector<Json::Value> {
                m_logger->flush();

                std::vector<Json::Value> records;
                std::istringstream lines(m_output.str());
                std::string line;
                while (std::getline(lines, line)) {
                    if (line.empty()) {
                        continue;
                    }

                    Json::CharReaderBuilder builder;
                    builder["collectComments"] = false;
                    const auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
                    Json::Value record;
                    std::string errors;
                    if (!reader->parse(
                            line.data(),
                            line.data() + line.size(),
                            &record,
                            &errors
                        )) {
                        ADD_FAILURE() << "Invalid structured log line: " << errors;
                        continue;
                    }
                    records.push_back(std::move(record));
                }

                m_output.str("");
                m_output.clear();
                EXPECT_EQ(records.size(), expected_count);
                return records;
            }

            static auto ExpectContext(
                const Json::Value& record,
                const char* request_id,
                const char* operation,
                std::string_view level,
                std::string_view message_marker
            ) -> void {
                if (request_id == nullptr) {
                    EXPECT_TRUE(record["request_id"].isNull());
                } else {
                    EXPECT_EQ(record["request_id"].asString(), request_id);
                }
                if (operation == nullptr) {
                    EXPECT_TRUE(record["operation"].isNull());
                } else {
                    EXPECT_EQ(record["operation"].asString(), operation);
                }
                EXPECT_EQ(record["instance_id"].asString(), "token-service-instance");
                EXPECT_EQ(record["level"].asString(), level);
                EXPECT_TRUE(Contains(record["message"].asString(), message_marker));
                for (const auto* field : {
                         "upload_id",
                         "job_id",
                         "lease_owner",
                         "state_version",
                     }) {
                    EXPECT_TRUE(record[field].isNull()) << field;
                }
            }

            static auto ExpectSecretsExcluded(
                const std::vector<Json::Value>& records,
                const std::vector<std::string>& secrets
            ) -> void {
                for (const auto& record : records) {
                    const auto serialized = Serialize(record);
                    for (const auto& secret : secrets) {
                        EXPECT_FALSE(secret.empty());
                        EXPECT_FALSE(Contains(serialized, secret)) << secret;
                    }
                }
            }

            std::shared_ptr<TokenService> m_service;
            std::shared_ptr<spdlog::logger> m_previous_logger;
            std::ostringstream m_output;
            std::shared_ptr<spdlog::sinks::ostream_sink_mt> m_sink;
            std::shared_ptr<spdlog::logger> m_logger;
        };

        TEST_F(TokenServiceLogContextTest, AuthAndShareSuccessPreserveCallerContext) {
            const auto auth_context = MakeContext("token-auth-request", "auth");
            const auto share_context = MakeContext("token-share-request", "share");

            auto [access_token, refresh_token] = m_service->GenerateTokens(
                42,
                "token-context-user",
                0,
                1,
                auth_context
            );
            ASSERT_TRUE(m_service->VerifyAccessToken(access_token, auth_context).has_value());
            ASSERT_TRUE(m_service->VerifyRefreshToken(refresh_token, auth_context).has_value());

            auto share_token = TokenService::GenerateShareToken(
                std::string(TEST_JWT_SECRET),
                "tokenctx",
                77,
                "download",
                share_context
            );
            ASSERT_TRUE(share_token.has_value());
            ASSERT_TRUE(TokenService::VerifyShareToken(std::string(TEST_JWT_SECRET), *share_token, share_context).has_value());

            const auto records = DrainRecords(7);
            ASSERT_EQ(records.size(), 7U);
            ExpectContext(records[0], "token-auth-request", "auth", "debug", "Generating token pair:");
            ExpectContext(records[1], "token-auth-request", "auth", "info", "op=jwt_verify");
            ExpectContext(records[2], "token-auth-request", "auth", "trace", "JWT verification successful:");
            ExpectContext(records[3], "token-auth-request", "auth", "info", "op=jwt_refresh_verify");
            ExpectContext(records[4], "token-auth-request", "auth", "trace", "Refresh token verification successful:");
            ExpectContext(records[5], "token-share-request", "share", "debug", "Share token generated successfully");
            ExpectContext(records[6], "token-share-request", "share", "debug", "Share token verification successful:");
            ExpectSecretsExcluded(
                records,
                {
                    std::string(TEST_JWT_SECRET),
                    access_token,
                    refresh_token,
                    *share_token,
                }
            );
        }

        TEST_F(TokenServiceLogContextTest, MalformedTokensPreserveContextWithoutEchoingValues) {
            const auto auth_context = MakeContext("token-invalid-auth", "auth");
            const auto share_context = MakeContext("token-invalid-share", "share");

            EXPECT_FALSE(m_service->VerifyAccessToken(
                                      std::string(RAW_ACCESS_TOKEN),
                                      auth_context
            )
                             .has_value());
            EXPECT_FALSE(m_service->VerifyRefreshToken(
                                      std::string(RAW_REFRESH_TOKEN),
                                      auth_context
            )
                             .has_value());
            EXPECT_FALSE(drogon::sync_wait(m_service->InvalidateAccessToken(std::string(RAW_REVOKE_TOKEN), auth_context)).has_value());
            EXPECT_FALSE(drogon::sync_wait(m_service->VerifyShareTokenWithRedis("", std::string(RAW_SHARE_TOKEN), share_context)).has_value());

            const auto records = DrainRecords(6);
            ASSERT_EQ(records.size(), 6U);
            ExpectContext(records[0], "token-invalid-auth", "auth", "info", "op=jwt_verify");
            ExpectContext(records[1], "token-invalid-auth", "auth", "warning", "JWT parsing failed");
            ExpectContext(records[2], "token-invalid-auth", "auth", "info", "op=jwt_refresh_verify");
            ExpectContext(records[3], "token-invalid-auth", "auth", "warning", "Refresh token parsing failed");
            ExpectContext(records[4], "token-invalid-auth", "auth", "warning", "Failed to extract JTI");
            ExpectContext(records[5], "token-invalid-share", "share", "warning", "Share token parsing failed");
            EXPECT_EQ(records[1]["message"].asString(), "JWT parsing failed");
            EXPECT_EQ(records[3]["message"].asString(), "Refresh token parsing failed");
            EXPECT_EQ(records[4]["message"].asString(), "Failed to extract JTI");
            EXPECT_EQ(records[5]["message"].asString(), "Share token parsing failed");
            ExpectSecretsExcluded(
                records,
                {
                    std::string(TEST_JWT_SECRET),
                    std::string(RAW_ACCESS_TOKEN),
                    std::string(RAW_REFRESH_TOKEN),
                    std::string(RAW_REVOKE_TOKEN),
                    std::string(RAW_SHARE_TOKEN),
                }
            );
        }

        TEST_F(TokenServiceLogContextTest, DefaultCallerKeepsNullRequestCorrelation) {
            EXPECT_FALSE(TokenService::VerifyShareToken(std::string(TEST_JWT_SECRET), std::string(RAW_SHARE_TOKEN)).has_value());

            const auto records = DrainRecords(1);
            ASSERT_EQ(records.size(), 1U);
            ExpectContext(
                records[0],
                nullptr,
                nullptr,
                "warning",
                "Share token parsing failed"
            );
            EXPECT_EQ(records[0]["message"].asString(), "Share token parsing failed");
            ExpectSecretsExcluded(
                records,
                {
                    std::string(TEST_JWT_SECRET),
                    std::string(RAW_SHARE_TOKEN),
                }
            );
        }

    } // namespace
} // namespace disk::services
