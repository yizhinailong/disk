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
#include <trantor/utils/Date.h>

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

        auto CallContainsContext(const std::string& source, std::string_view call_marker) -> bool {
            const auto begin = source.find(call_marker);
            if (begin == std::string::npos) {
                return false;
            }
            const auto end = source.find(");", begin);
            return end != std::string::npos &&
                   source.substr(begin, end - begin).find("log_context") != std::string::npos;
        }

        TEST(ShareImplementationHelperContractTest, SingleConsumerHelpersHaveInternalLinkage) {
            const auto service_header = ReadSourceFile("src/services/ShareService.hpp");
            const auto service_source = ReadSourceFile("src/services/ShareService.cpp");

            EXPECT_FALSE(Contains(service_header, "GenerateShareCode"));
            EXPECT_FALSE(Contains(service_header, "GetStatusFilter"));
            EXPECT_FALSE(Contains(service_source, "ShareService::GenerateShareCode"));
            EXPECT_FALSE(Contains(service_source, "ShareService::GetStatusFilter"));
            EXPECT_TRUE(Contains(
                service_source,
                "[[nodiscard]] auto GenerateShareCode() -> std::string"
            ));
            EXPECT_TRUE(Contains(
                service_source,
                "[[nodiscard]] auto GetStatusFilter(const std::string& status)"
            ));
            EXPECT_EQ(CountOccurrences(service_source, "GenerateShareCode("), 2U);
            EXPECT_EQ(CountOccurrences(service_source, "GetStatusFilter("), 2U);
            EXPECT_TRUE(Contains(
                service_source,
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
            ));
            EXPECT_TRUE(Contains(service_source, "constexpr int code_length = 8"));
            EXPECT_FALSE(Contains(service_source, "#include <random>"));
            EXPECT_FALSE(Contains(service_source, "std::random_device"));
            EXPECT_FALSE(Contains(service_source, "std::mt19937"));
            EXPECT_FALSE(Contains(service_source, "std::uniform_int_distribution"));
            EXPECT_TRUE(Contains(
                service_source,
                "randombytes_uniform(static_cast<uint32_t>(chars.size()))"
            ));
            EXPECT_EQ(CountOccurrences(service_source, "randombytes_uniform("), 1U);
            EXPECT_TRUE(Contains(
                service_source,
                "constexpr int SHARE_CODE_GENERATION_MAX_ATTEMPTS = 5"
            ));
            EXPECT_TRUE(Contains(
                service_source,
                "ON CONFLICT (share_code) DO NOTHING RETURNING *"
            ));
            EXPECT_TRUE(Contains(
                service_source,
                "attempt <= SHARE_CODE_GENERATION_MAX_ATTEMPTS"
            ));
            EXPECT_TRUE(Contains(service_source, "if (!insert_result.empty())"));
            EXPECT_TRUE(Contains(service_source, "if (share_code.empty())"));

            const auto create_body = SourceSection(
                service_source,
                "auto ShareService::Create(",
                "auto ShareService::List("
            );
            EXPECT_FALSE(Contains(create_body, "CoroMapper<Shares> share_mapper"));
            const auto candidate_insert = create_body.find("ON CONFLICT (share_code)");
            const auto exhausted_check = create_body.find("if (share_code.empty())");
            const auto association_insert = create_body.find("INSERT INTO share_files");
            ASSERT_NE(candidate_insert, std::string::npos);
            ASSERT_NE(exhausted_check, std::string::npos);
            ASSERT_NE(association_insert, std::string::npos);
            EXPECT_LT(candidate_insert, exhausted_check);
            EXPECT_LT(exhausted_check, association_insert);
            EXPECT_TRUE(Contains(service_source, "if (status == \"all\")"));
            EXPECT_TRUE(Contains(service_source, "if (status == \"active\")"));
            EXPECT_TRUE(Contains(service_source, "if (status == \"expired\")"));
            EXPECT_TRUE(Contains(service_source, "if (status == \"cancelled\")"));
            EXPECT_EQ(CountOccurrences(service_source, "return std::nullopt;"), 2U);
            EXPECT_TRUE(Contains(service_source, "ShareStatus::Active"));
            EXPECT_TRUE(Contains(service_source, "ShareStatus::Expired"));
            EXPECT_TRUE(Contains(service_source, "ShareStatus::Cancelled"));
        }

        TEST(ShareImplementationHelperContractTest, StatelessDomainHelpersHaveInternalLinkage) {
            const auto service_header = ReadSourceFile("src/services/ShareService.hpp");
            const auto service_source = ReadSourceFile("src/services/ShareService.cpp");
            const auto anonymous_helpers =
                SourceSection(service_source, "    namespace {", "    } // namespace");

            ASSERT_FALSE(anonymous_helpers.empty());
            for (const auto* helper : {
                     "IsShareExpired",
                     "IsShareActive",
                     "VerifyPassword",
                     "FormatDateTime",
                     "BuildShareLink",
                 }) {
                EXPECT_FALSE(Contains(service_header, helper)) << helper;
                EXPECT_FALSE(Contains(service_source, std::string("ShareService::") + helper))
                    << helper;
            }

            EXPECT_TRUE(Contains(
                anonymous_helpers,
                "[[nodiscard]] auto IsShareExpired(const Shares& share) -> bool"
            ));
            EXPECT_TRUE(Contains(
                anonymous_helpers,
                "[[nodiscard]] auto IsShareActive(const Shares& share) -> bool"
            ));
            EXPECT_TRUE(Contains(
                anonymous_helpers,
                "[[nodiscard]] auto VerifyPassword(const Shares& share, const std::string& password) -> bool"
            ));
            EXPECT_TRUE(Contains(
                anonymous_helpers,
                "[[nodiscard]] auto FormatDateTime(const trantor::Date& date) -> std::string"
            ));
            EXPECT_TRUE(Contains(
                anonymous_helpers,
                "[[nodiscard]] auto BuildShareLink(const std::string& share_code) -> std::string"
            ));

            EXPECT_EQ(CountOccurrences(service_source, "IsShareExpired("), 5U);
            EXPECT_EQ(CountOccurrences(service_source, "IsShareActive("), 4U);
            EXPECT_EQ(CountOccurrences(service_source, "VerifyPassword("), 3U);
            EXPECT_EQ(CountOccurrences(service_source, "FormatDateTime("), 9U);
            EXPECT_EQ(CountOccurrences(service_source, "BuildShareLink("), 4U);

            EXPECT_TRUE(Contains(anonymous_helpers, "share.getExpiresAt() == nullptr"));
            EXPECT_TRUE(Contains(
                anonymous_helpers,
                "share.getValueOfExpiresAt() < trantor::Date::now()"
            ));
            EXPECT_TRUE(Contains(
                anonymous_helpers,
                "share.getValueOfStatus() != static_cast<int8_t>(ShareStatus::Active)"
            ));
            EXPECT_TRUE(Contains(anonymous_helpers, "share.getPasswordHash() == nullptr"));
            EXPECT_TRUE(Contains(
                anonymous_helpers,
                "utils::HashUtil::VerifyPassword(password, share.getValueOfPasswordHash())"
            ));
            EXPECT_FALSE(Contains(anonymous_helpers, "std::localtime("));
            EXPECT_FALSE(Contains(anonymous_helpers, "std::put_time("));
            EXPECT_FALSE(Contains(anonymous_helpers, "toDbStringLocal()"));
            EXPECT_TRUE(Contains(
                anonymous_helpers,
                "return date.toCustomFormattedStringLocal(\"%Y-%m-%d %H:%M:%S\", false);"
            ));
            EXPECT_EQ(CountOccurrences(service_source, "toCustomFormattedStringLocal("), 1U);
            EXPECT_TRUE(Contains(anonymous_helpers, "return \"/s/\" + share_code"));
        }

        TEST(ShareDateTimeFormatContractTest, TrantorCustomFormatterKeepsSecondPrecision) {
            constexpr int64_t timestamp_with_microseconds = 1'234'567;
            const auto formatted = trantor::Date(timestamp_with_microseconds)
                                       .toCustomFormattedStringLocal(
                                           "%Y-%m-%d %H:%M:%S",
                                           false
                                       );

            ASSERT_EQ(formatted.size(), 19U);
            EXPECT_EQ(formatted[4], '-');
            EXPECT_EQ(formatted[7], '-');
            EXPECT_EQ(formatted[10], ' ');
            EXPECT_EQ(formatted[13], ':');
            EXPECT_EQ(formatted[16], ':');
            EXPECT_EQ(formatted.find('.'), std::string::npos);
        }

        TEST(ShareCreateLogContractTest, TransactionExceptionsUseFixedSummaries) {
            const auto service_source = ReadSourceFile("src/services/ShareService.cpp");
            const auto create_body = SourceSection(
                service_source,
                "auto ShareService::Create(",
                "    auto ShareService::List("
            );

            ASSERT_FALSE(create_body.empty());
            EXPECT_EQ(CountOccurrences(create_body, ".what()"), 0U);
            EXPECT_EQ(
                CountOccurrences(
                    create_body,
                    "Logger::Error(log_context) << \"Failed to create share (transaction)\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    create_body,
                    "Logger::Error(log_context) << \"Transaction rollback failed\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    create_body,
                    "ErrorInfo(ErrorCode::InternalError, \"Failed to create share\")"
                ),
                3U
            );
            EXPECT_EQ(CountOccurrences(create_body, "transaction->rollback();"), 2U);
        }

        TEST(ShareManagementLogContractTest, WriteExceptionsUseFixedSummaries) {
            const auto service_source = ReadSourceFile("src/services/ShareService.cpp");
            const auto update_body = SourceSection(
                service_source,
                "auto ShareService::Update(",
                "    auto ShareService::Cancel("
            );
            const auto cancel_body = SourceSection(
                service_source,
                "auto ShareService::Cancel(",
                "    auto ShareService::Access("
            );

            ASSERT_FALSE(update_body.empty());
            ASSERT_FALSE(cancel_body.empty());
            EXPECT_EQ(CountOccurrences(update_body, ".what()"), 0U);
            EXPECT_EQ(CountOccurrences(cancel_body, ".what()"), 0U);
            EXPECT_EQ(
                CountOccurrences(
                    update_body,
                    "Logger::Error(log_context) << \"Failed to update share\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    cancel_body,
                    "Logger::Error(log_context) << \"Failed to fetch shares for cancel\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    cancel_body,
                    "Logger::Error(log_context) << \"Failed to cancel share\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    update_body,
                    "ErrorInfo(ErrorCode::InternalError, \"Failed to update share\")"
                ),
                1U
            );
            EXPECT_EQ(CountOccurrences(cancel_body, "\"Operation failed\""), 2U);
            EXPECT_EQ(CountOccurrences(cancel_body, "response.summary.succeeded--;"), 1U);
            EXPECT_EQ(
                CountOccurrences(cancel_body, "co_await m_audit_service.RecordCancel("),
                2U
            );
        }

        TEST(SharePublicReadLogContractTest, QueryExceptionsUseFixedSummaries) {
            const auto service_source = ReadSourceFile("src/services/ShareService.cpp");
            const auto browse_body = SourceSection(
                service_source,
                "auto ShareService::Browse(",
                "    auto ShareService::GetDownloadInfo("
            );
            const auto download_body = SourceSection(
                service_source,
                "auto ShareService::GetDownloadInfo(",
                "    auto ShareService::CompleteDownload("
            );

            ASSERT_FALSE(browse_body.empty());
            ASSERT_FALSE(download_body.empty());
            EXPECT_EQ(CountOccurrences(browse_body, ".what()"), 0U);
            EXPECT_EQ(CountOccurrences(download_body, ".what()"), 0U);
            EXPECT_EQ(
                CountOccurrences(
                    browse_body,
                    "Logger::Error(log_context) << \"Failed to browse share folder\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    download_body,
                    "Logger::Error(log_context) << \"Failed to get download info\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    browse_body,
                    "ErrorInfo(ErrorCode::InternalError, \"Failed to browse share content\")"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    download_body,
                    "ErrorInfo(ErrorCode::InternalError, \"Failed to get download info\")"
                ),
                1U
            );
            EXPECT_EQ(CountOccurrences(browse_body, "BuildSharedFolderAccessPredicate("), 2U);
            EXPECT_EQ(CountOccurrences(download_body, "BuildSharedFileAccessPredicate("), 1U);
        }

        TEST(ShareSaveLogContractTest, TransactionExceptionsUseFixedSummaries) {
            const auto service_source = ReadSourceFile("src/services/ShareService.cpp");
            const auto save_body = SourceSection(
                service_source,
                "auto ShareService::SaveToDrive(",
                "    auto ShareService::FindShareByCode("
            );

            ASSERT_FALSE(save_body.empty());
            EXPECT_EQ(CountOccurrences(save_body, ".what()"), 0U);
            EXPECT_EQ(
                CountOccurrences(
                    save_body,
                    "Logger::Error(log_context) << \"Failed to save share items\";"
                ),
                2U
            );
            EXPECT_EQ(
                CountOccurrences(
                    save_body,
                    "Logger::Error(log_context) << \"Transaction rollback failed\";"
                ),
                2U
            );
            EXPECT_EQ(
                CountOccurrences(
                    save_body,
                    "ErrorInfo(ErrorCode::InternalError, \"Failed to save share items\")"
                ),
                3U
            );
            EXPECT_EQ(CountOccurrences(save_body, "transaction->rollback();"), 3U);
            EXPECT_EQ(CountOccurrences(save_body, "TransactionRunner::Commit("), 1U);
            EXPECT_EQ(CountOccurrences(save_body, "content_service.IncrementRefCount("), 2U);
            EXPECT_EQ(CountOccurrences(save_body, "FileListCache::Invalidate("), 1U);
        }

        TEST(ShareHelperLogContractTest, DatabaseExceptionsUseFixedSummaries) {
            const auto service_source = ReadSourceFile("src/services/ShareService.cpp");
            const auto helper_body = SourceSection(
                service_source,
                "auto ShareService::FindShareByCode(",
                "    auto ShareService::HandleFailedShareAccess("
            );

            ASSERT_FALSE(helper_body.empty());
            EXPECT_EQ(CountOccurrences(helper_body, ".what()"), 0U);
            for (const auto* summary : {
                     "Failed to find share",
                     "Failed to validate file ownership",
                     "Failed to validate folder ownership",
                     "Failed to validate share active state",
                     "Failed to update view count",
                     "Failed to update download count",
                     "Failed to update shared file download metadata",
                 }) {
                EXPECT_EQ(
                    CountOccurrences(
                        helper_body,
                        std::string("Logger::Error(log_context) << \"") + summary + "\";"
                    ),
                    1U
                ) << summary;
            }
            EXPECT_EQ(
                CountOccurrences(
                    helper_body,
                    "Logger::Error(log_context) << \"Failed to get share file list\";"
                ),
                2U
            );
            EXPECT_FALSE(Contains(helper_body, "(file_id="));
            for (const auto* public_error : {
                     "Failed to find share",
                     "Failed to validate file ownership",
                     "Failed to validate folder ownership",
                     "Failed to validate share status",
                 }) {
                EXPECT_EQ(
                    CountOccurrences(
                        helper_body,
                        std::string("ErrorInfo(ErrorCode::InternalError, \"") + public_error + "\")"
                    ),
                    1U
                ) << public_error;
            }
        }

        TEST(ShareReadLogContractTest, ListExceptionsUseFixedSummaries) {
            const auto service_source = ReadSourceFile("src/services/ShareService.cpp");
            const auto list_body = SourceSection(
                service_source,
                "auto ShareService::List(",
                "    auto ShareService::Detail("
            );

            ASSERT_FALSE(list_body.empty());
            EXPECT_EQ(CountOccurrences(list_body, ".what()"), 0U);
            EXPECT_EQ(
                CountOccurrences(
                    list_body,
                    "Logger::Error(log_context) << \"Failed to get share count\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    list_body,
                    "Logger::Error(log_context) << \"Failed to get share list\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    list_body,
                    "ErrorInfo(ErrorCode::InternalError, \"Failed to get share list\")"
                ),
                2U
            );
        }

        TEST(ShareCreateControllerValueLogContractTest, CreateUsesFixedSummaries) {
            const auto controller_source = ReadSourceFile("src/controllers/ShareController.cpp");
            const auto create_controller = SourceSection(
                controller_source,
                "auto ShareController::Create(",
                "    auto ShareController::List("
            );

            ASSERT_FALSE(create_controller.empty());
            for (const auto* fixed_log : {
                     "Logger::Info(log_context) << \"Received create share request\";",
                     "Logger::Warn(log_context) << \"Create share request parameter validation failed\";",
                     "Logger::Debug(log_context) << \"Create share parameter validation passed\";",
                     "Logger::Error(log_context) << \"Create share failed\";",
                     "Logger::Info(log_context) << \"Create share successful\";",
                 }) {
                EXPECT_EQ(CountOccurrences(create_controller, fixed_log), 1U) << fixed_log;
            }
            for (const auto* raw_boundary_log : {
                     "<< \"Received create share request: \" << request->getPeerAddr().toIpPort()",
                     "<< \"Create share request parameter validation failed: \"",
                     "<< \"Create share parameter validation passed: file_ids.size()=\"",
                     "<< \"Create share failed: \" << result.error().message",
                     "<< \"Create share successful: share_id=\" << result->share_id",
                 }) {
                EXPECT_EQ(CountOccurrences(create_controller, raw_boundary_log), 0U)
                    << raw_boundary_log;
            }

            EXPECT_EQ(CountOccurrences(create_controller, "Logger::Info(log_context)"), 2U);
            EXPECT_EQ(CountOccurrences(create_controller, "Logger::Warn(log_context)"), 1U);
            EXPECT_EQ(CountOccurrences(create_controller, "Logger::Debug(log_context)"), 1U);
            EXPECT_EQ(CountOccurrences(create_controller, "Logger::Error(log_context)"), 1U);
            for (const auto* preserved_controller_step : {
                     "GetRequestLogContext(request, \"share\")",
                     "CreateShareRequest::FromRequest(request, log_context)",
                     "if (!parse_result)",
                     "co_return Response::Error(parse_result.error());",
                     "GetAuthenticatedUserId(request)",
                     "m_share_service->Create(",
                     "BuildAuditContext(request)",
                     "if (!result)",
                     "co_return Response::Error(result.error());",
                     "co_return Response::Success(result->ToJson());",
                 }) {
                EXPECT_EQ(CountOccurrences(create_controller, preserved_controller_step), 1U)
                    << preserved_controller_step;
            }
        }

        TEST(ShareListControllerValueLogContractTest, ListUsesFixedSummaries) {
            const auto controller_source = ReadSourceFile("src/controllers/ShareController.cpp");
            const auto list_controller = SourceSection(
                controller_source,
                "auto ShareController::List(",
                "    auto ShareController::Detail("
            );

            ASSERT_FALSE(list_controller.empty());
            for (const auto* fixed_log : {
                     "Logger::Info(log_context) << \"Received get share list request\";",
                     "Logger::Warn(log_context) << \"Share list request parameter validation failed\";",
                     "Logger::Debug(log_context) << \"Share list parameter validation passed\";",
                     "Logger::Error(log_context) << \"Get share list failed\";",
                     "Logger::Info(log_context) << \"Get share list successful\";",
                 }) {
                EXPECT_EQ(CountOccurrences(list_controller, fixed_log), 1U) << fixed_log;
            }
            for (const auto* raw_boundary_log : {
                     "<< \"Received get share list request: \" << request->getPeerAddr().toIpPort()",
                     "<< \"Share list request parameter validation failed: \"",
                     "<< \"Share list parameter validation passed: status=\" << parse_result->status",
                     "<< \"Get share list failed: \" << result.error().message",
                     "<< \"Get share list successful: items=\" << result->items.size()",
                 }) {
                EXPECT_EQ(CountOccurrences(list_controller, raw_boundary_log), 0U)
                    << raw_boundary_log;
            }

            EXPECT_EQ(CountOccurrences(list_controller, "Logger::Info(log_context)"), 2U);
            EXPECT_EQ(CountOccurrences(list_controller, "Logger::Warn(log_context)"), 1U);
            EXPECT_EQ(CountOccurrences(list_controller, "Logger::Debug(log_context)"), 1U);
            EXPECT_EQ(CountOccurrences(list_controller, "Logger::Error(log_context)"), 1U);
            for (const auto* preserved_controller_step : {
                     "GetRequestLogContext(request, \"share\")",
                     "ShareListRequest::FromRequest(request, log_context)",
                     "if (!parse_result)",
                     "co_return Response::Error(parse_result.error());",
                     "GetAuthenticatedUserId(request)",
                     "m_share_service->List(*parse_result, user_id, log_context)",
                     "if (!result)",
                     "co_return Response::Error(result.error());",
                     "co_return Response::Success(result->ToJson());",
                 }) {
                EXPECT_EQ(CountOccurrences(list_controller, preserved_controller_step), 1U)
                    << preserved_controller_step;
            }
        }

        TEST(ShareDetailControllerValueLogContractTest, DetailUsesFixedSummaries) {
            const auto controller_source = ReadSourceFile("src/controllers/ShareController.cpp");
            const auto detail_controller = SourceSection(
                controller_source,
                "auto ShareController::Detail(",
                "    auto ShareController::Update("
            );

            ASSERT_FALSE(detail_controller.empty());
            for (const auto* fixed_log : {
                     "Logger::Info(log_context) << \"Received get share details request\";",
                     "Logger::Warn(log_context) << \"Share detail request parameter validation failed\";",
                     "Logger::Error(log_context) << \"Get share details failed\";",
                     "Logger::Info(log_context) << \"Get share details successful\";",
                 }) {
                EXPECT_EQ(CountOccurrences(detail_controller, fixed_log), 1U) << fixed_log;
            }
            for (const auto* raw_boundary_log : {
                     "<< \"Received get share details request: \" << request->getPeerAddr().toIpPort()",
                     "<< \"Share detail request parameter validation failed: \"",
                     "<< \"Get share details failed: \" << result.error().message",
                     "<< \"Get share details successful: share_id=\" << share_id",
                 }) {
                EXPECT_EQ(CountOccurrences(detail_controller, raw_boundary_log), 0U)
                    << raw_boundary_log;
            }

            EXPECT_EQ(CountOccurrences(detail_controller, "Logger::Info(log_context)"), 2U);
            EXPECT_EQ(CountOccurrences(detail_controller, "Logger::Warn(log_context)"), 1U);
            EXPECT_EQ(CountOccurrences(detail_controller, "Logger::Debug(log_context)"), 0U);
            EXPECT_EQ(CountOccurrences(detail_controller, "Logger::Error(log_context)"), 1U);
            for (const auto* preserved_controller_step : {
                     "GetRequestLogContext(request, \"share\")",
                     "ShareDetailRequest::FromPath(share_id, log_context)",
                     "if (!parse_result)",
                     "co_return Response::Error(parse_result.error());",
                     "GetAuthenticatedUserId(request)",
                     "m_share_service->Detail(*parse_result, user_id, log_context)",
                     "if (!result)",
                     "co_return Response::Error(result.error());",
                     "co_return Response::Success(result->ToJson());",
                 }) {
                EXPECT_EQ(CountOccurrences(detail_controller, preserved_controller_step), 1U)
                    << preserved_controller_step;
            }
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

            const auto service_class_begin = service_header.find("    class ShareService {");
            ASSERT_NE(service_class_begin, std::string::npos);
            const auto service_private_begin =
                service_header.find("    private:", service_class_begin);
            ASSERT_NE(service_private_begin, std::string::npos);
            const auto public_service_interface = service_header.substr(
                service_class_begin,
                service_private_begin - service_class_begin
            );
            EXPECT_FALSE(Contains(public_service_interface, "auto FindShareByCode("));
            EXPECT_TRUE(Contains(public_service_interface, "auto CompleteDownload("));

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
            EXPECT_FALSE(Contains(service_source, "BatchUtils::ValidateBatchInput("));
            EXPECT_FALSE(Contains(service_source, "std::numeric_limits<size_t>::max()"));
            EXPECT_TRUE(Contains(service_source, "if (request.share_ids.empty())"));
            EXPECT_TRUE(Contains(
                service_source,
                "BatchUtils::Chunk(request.share_ids, DEFAULT_BATCH_CHUNK_SIZE)"
            ));
            EXPECT_TRUE(Contains(service_source, "BatchUtils::BuildInPlaceholders(chunk)"));

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
            EXPECT_TRUE(Contains(
                audit_source,
                "disk::utils::SetRequestCorrelationFields(details, log_context);"
            ));
            EXPECT_EQ(CountOccurrences(audit_source, ".what()"), 0);
            EXPECT_EQ(
                CountOccurrences(
                    audit_source,
                    "Logger::Error(log_context) << \"Failed to record share audit event\";"
                ),
                2
            );

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

        TEST(ShareSqlBindingContractTest, CreateUsesSharedBatchBinder) {
            const auto utils_header = ReadSourceFile("src/services/FileServiceUtils.hpp");
            const auto service_source = ReadSourceFile("src/services/ShareService.cpp");

            EXPECT_EQ(CountOccurrences(utils_header, "auto ExecSqlWithBindings("), 1U);
            EXPECT_TRUE(Contains(utils_header, "auto binder = *client << sql;"));
            EXPECT_TRUE(Contains(utils_header, "bind_parameters(binder);"));
            EXPECT_TRUE(Contains(
                utils_header,
                "co_return co_await drogon::orm::internal::SqlAwaiter(std::move(binder));"
            ));
            EXPECT_EQ(
                CountOccurrences(service_source, "template <typename BindParameters>"),
                0U
            );
            EXPECT_EQ(CountOccurrences(service_source, "auto ExecSqlWithBindings("), 0U);
            EXPECT_EQ(
                CountOccurrences(
                    service_source,
                    "disk::file::utils::ExecSqlWithBindings("
                ),
                2U
            );
        }

    } // namespace
} // namespace disk::share
