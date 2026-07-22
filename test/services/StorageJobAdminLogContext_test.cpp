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
                3
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

    } // namespace
} // namespace disk::jobs
