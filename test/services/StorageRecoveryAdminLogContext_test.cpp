/**
 * @file StorageRecoveryAdminLogContext_test.cpp
 * @brief Storage recovery administration request correlation source contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace disk::recovery {
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

        TEST(StorageRecoveryAdminLogContextContractTest, RequestBoundariesUseTypedContext) {
            const auto controller_source =
                ReadSourceFile("src/controllers/StorageRecoveryAdminController.cpp");
            const auto service_header =
                ReadSourceFile("src/services/StorageRecoveryAdminService.hpp");
            const auto service_source =
                ReadSourceFile("src/services/StorageRecoveryAdminService.cpp");

            ASSERT_FALSE(controller_source.empty());
            ASSERT_FALSE(service_header.empty());
            ASSERT_FALSE(service_source.empty());

            EXPECT_EQ(
                CountOccurrences(
                    controller_source,
                    "GetRequestLogContext(request, \"admin\")"
                ),
                3
            );
            EXPECT_EQ(
                CountOccurrences(
                    controller_source,
                    "log_context.upload_id = parsed->upload_id;"
                ),
                2
            );
            for (const auto* call_marker : {
                     "BuildService().ReleaseUploadLease(",
                     "BuildService().RebuildUploadCleanup(",
                     "BuildService().EnqueueReconciliation(",
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
                CountOccurrences(
                    service_source,
                    "log_context.upload_id = request.upload_id;"
                ),
                2
            );
            EXPECT_EQ(CountOccurrences(service_source, "co_await RecordAudit("), 3);
            EXPECT_TRUE(AllCallsContainContext(service_source, "co_await RecordAudit("));
            EXPECT_TRUE(AllLoggerCallsUseContext(service_source));

            EXPECT_TRUE(Contains(service_source, "SetLogContext(details, log_context);"));
            EXPECT_TRUE(Contains(service_source, "details[\"request_id\"]"));
            EXPECT_TRUE(Contains(service_source, "details[\"operation\"]"));
            EXPECT_TRUE(Contains(service_source, "disk::upload::IsTerminalStatus(status)"));
            EXPECT_TRUE(Contains(service_source, "log_context.state_version = response.state_version;"));
            EXPECT_TRUE(Contains(service_source, "log_context.lease_owner = response.lease_owner;"));
            EXPECT_TRUE(Contains(service_source, "log_context.job_id = response.job_id;"));

            EXPECT_FALSE(Contains(service_source, "log_context.state_version = request.expected_state_version"));
            EXPECT_FALSE(Contains(service_source, "log_context.lease_owner = request.expected_lease_owner"));
            EXPECT_FALSE(Contains(service_source, "log_context.upload_id = request.scan_id"));
            EXPECT_FALSE(Contains(service_source, "log_context.job_id = request."));
            EXPECT_FALSE(Contains(controller_source, "Authorization"));
            EXPECT_FALSE(Contains(service_source, "Authorization"));
        }

    } // namespace
} // namespace disk::recovery
