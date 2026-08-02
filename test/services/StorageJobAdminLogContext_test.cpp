/**
 * @file StorageJobAdminLogContext_test.cpp
 * @brief Storage job administration request correlation source contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace disk::jobs {
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

        auto AllLoggerCallsUseContext(const std::string& source) -> bool {
            size_t position = 0;
            size_t count = 0;
            while ((position = source.find("Logger::", position)) != std::string::npos) {
                const auto end = source.find(')', position);
                if (end == std::string::npos ||
                    source.substr(position, end - position).find("log_context") ==
                        std::string::npos) {
                    return false;
                }
                ++count;
                position = end + 1;
            }
            return count > 0;
        }

        TEST(StorageJobAdminLogContextContractTest, NullableRowsUseSharedExtractor) {
            const auto helper_header = ReadSourceFile("src/utils/DbRowUtils.hpp");
            const auto job_source =
                ReadSourceFile("src/services/StorageJobAdminService.cpp");
            const auto recovery_source =
                ReadSourceFile("src/services/StorageRecoveryAdminService.cpp");
            const auto diagnostic_source =
                ReadSourceFile("src/services/UploadDiagnosticService.cpp");

            EXPECT_FALSE(helper_header.empty());
            EXPECT_EQ(CountOccurrences(helper_header, "auto OptionalRowValue("), 1U);
            EXPECT_EQ(CountOccurrences(helper_header, "row[field].isNull()"), 1U);
            EXPECT_EQ(
                CountOccurrences(helper_header, "row[field].template as<T>()"),
                1U
            );

            for (const auto* source : {
                     &job_source,
                     &recovery_source,
                     &diagnostic_source,
                 }) {
                EXPECT_EQ(CountOccurrences(*source, "#include \"utils/DbRowUtils.hpp\""), 1U);
            }
            EXPECT_EQ(
                CountOccurrences(job_source, "disk::utils::OptionalRowValue<"),
                4U
            );
            EXPECT_EQ(
                CountOccurrences(recovery_source, "disk::utils::OptionalRowValue<"),
                2U
            );
            EXPECT_EQ(
                CountOccurrences(diagnostic_source, "disk::utils::OptionalRowValue<"),
                10U
            );
            EXPECT_FALSE(Contains(job_source, "auto OptionalString("));
            EXPECT_FALSE(Contains(recovery_source, "auto OptionalValue("));
            EXPECT_FALSE(Contains(diagnostic_source, "auto OptionalValue("));
        }

        TEST(StorageJobAdminLogContextContractTest, RequestBoundariesUseTypedContext) {
            const auto controller_source =
                ReadSourceFile("src/controllers/StorageJobAdminController.cpp");
            const auto service_header =
                ReadSourceFile("src/services/StorageJobAdminService.hpp");
            const auto service_source =
                ReadSourceFile("src/services/StorageJobAdminService.cpp");
            const auto list_and_get_source = ExtractRange(
                service_source,
                "auto StorageJobAdminService::List(",
                "    auto StorageJobAdminService::ListRelatedToUpload("
            );
            const auto replay_source = ExtractRange(
                service_source,
                "auto StorageJobAdminService::Replay(",
                "\n} // namespace disk::jobs"
            );
            const auto audit_source = ExtractRange(
                service_source,
                "Json::Value details(Json::objectValue);",
                "            auto inserted ="
            );

            ASSERT_FALSE(controller_source.empty());
            ASSERT_FALSE(service_header.empty());
            ASSERT_FALSE(list_and_get_source.empty());
            ASSERT_FALSE(replay_source.empty());
            ASSERT_FALSE(audit_source.empty());

            EXPECT_EQ(
                CountOccurrences(
                    controller_source,
                    "GetRequestLogContext(request, \"admin\")"
                ),
                3
            );
            EXPECT_EQ(
                CountOccurrences(controller_source, "log_context.job_id = job_id.value();"),
                2
            );
            for (const auto* call_marker : {
                     "BuildService().List(",
                     "BuildService().Get(",
                     "BuildService().Replay(",
                 }) {
                EXPECT_TRUE(AllCallsContainContext(controller_source, call_marker))
                    << call_marker;
            }
            EXPECT_TRUE(AllLoggerCallsUseContext(controller_source));

            EXPECT_EQ(
                CountOccurrences(
                    service_header,
                    "disk::utils::LogContext log_context = {}"
                ),
                4
            );
            EXPECT_EQ(
                CountOccurrences(service_source, "log_context.job_id = job_id;"),
                2
            );
            EXPECT_TRUE(AllLoggerCallsUseContext(list_and_get_source));
            EXPECT_TRUE(AllLoggerCallsUseContext(replay_source));
            EXPECT_TRUE(AllCallsContainContext(replay_source, "co_await Get("));
            EXPECT_TRUE(AllCallsContainContext(replay_source, "co_await ReplayInTransaction("));

            EXPECT_TRUE(Contains(service_source, "SetLogContext(details, log_context);"));
            EXPECT_TRUE(Contains(service_source, "details[\"request_id\"]"));
            EXPECT_TRUE(Contains(service_source, "details[\"operation\"]"));
            EXPECT_TRUE(Contains(audit_source, "details[\"job_id\"]"));
            EXPECT_FALSE(Contains(audit_source, "details[\"payload\"]"));
            EXPECT_FALSE(Contains(service_source, "std::format("));
            EXPECT_FALSE(Contains(controller_source, "Authorization"));
            EXPECT_FALSE(Contains(service_source, "Authorization"));
        }

        TEST(StorageJobAdminLogContextContractTest, UploadDiagnosticsPreserveTypedContext) {
            const auto controller_source =
                ReadSourceFile("src/controllers/UploadDiagnosticController.cpp");
            const auto diagnostic_header =
                ReadSourceFile("src/services/UploadDiagnosticService.hpp");
            const auto diagnostic_source =
                ReadSourceFile("src/services/UploadDiagnosticService.cpp");
            const auto storage_job_header =
                ReadSourceFile("src/services/StorageJobAdminService.hpp");
            const auto storage_job_source =
                ReadSourceFile("src/services/StorageJobAdminService.cpp");
            const auto related_job_source = ExtractRange(
                storage_job_source,
                "auto StorageJobAdminService::ListRelatedToUpload(",
                "    auto StorageJobAdminService::Replay("
            );

            ASSERT_FALSE(controller_source.empty());
            ASSERT_FALSE(diagnostic_header.empty());
            ASSERT_FALSE(diagnostic_source.empty());
            ASSERT_FALSE(storage_job_header.empty());
            ASSERT_FALSE(related_job_source.empty());

            const auto context_position =
                controller_source.find("GetRequestLogContext(request, \"admin\")");
            const auto parse_position = controller_source.find(
                "UploadDiagnosticRequest::FromRequest(request, upload_id)"
            );
            const auto upload_position =
                controller_source.find("log_context.upload_id = parsed->upload_id;");
            ASSERT_NE(context_position, std::string::npos);
            ASSERT_NE(parse_position, std::string::npos);
            ASSERT_NE(upload_position, std::string::npos);
            EXPECT_LT(context_position, parse_position);
            EXPECT_LT(parse_position, upload_position);
            EXPECT_TRUE(AllCallsContainContext(controller_source, "service.Diagnose("));
            EXPECT_TRUE(AllLoggerCallsUseContext(controller_source));

            EXPECT_EQ(
                CountOccurrences(
                    diagnostic_header,
                    "disk::utils::LogContext log_context = {}"
                ),
                1
            );
            EXPECT_TRUE(Contains(
                diagnostic_source,
                "log_context.upload_id == response.task.upload_id"
            ));
            EXPECT_TRUE(Contains(
                diagnostic_source,
                "log_context.state_version = response.task.state_version;"
            ));
            EXPECT_TRUE(Contains(
                diagnostic_source,
                "log_context.lease_owner = response.task.lease->owner;"
            ));
            EXPECT_TRUE(AllCallsContainContext(diagnostic_source, "HeadChunkObject("));
            EXPECT_TRUE(AllCallsContainContext(diagnostic_source, "ListRelatedToUpload("));
            EXPECT_TRUE(AllLoggerCallsUseContext(diagnostic_source));

            EXPECT_TRUE(Contains(
                storage_job_header,
                "int page_size,\n            disk::utils::LogContext log_context = {}"
            ));
            EXPECT_TRUE(AllLoggerCallsUseContext(related_job_source));
            EXPECT_FALSE(Contains(related_job_source, "log_context.job_id"));
            EXPECT_FALSE(Contains(related_job_source, "error.what()"));
            EXPECT_FALSE(Contains(diagnostic_source, "error.what()"));
            EXPECT_FALSE(Contains(diagnostic_source, "Logger::Error()"));
            EXPECT_FALSE(Contains(related_job_source, "Logger::Error()"));
            EXPECT_FALSE(Contains(controller_source, "Authorization"));
            EXPECT_FALSE(Contains(diagnostic_source, "Authorization"));
            EXPECT_FALSE(Contains(related_job_source, "Authorization"));
        }

    } // namespace
} // namespace disk::jobs
