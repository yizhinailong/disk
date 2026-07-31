/**
 * @file TrashLogContext_test.cpp
 * @brief Trash request correlation source contract tests
 *
 * @copyright Copyright (c) 2026
 */

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace disk::trash {
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

        auto CallContainsContext(const std::string& source, std::string_view call_marker) -> bool {
            const auto begin = source.find(call_marker);
            if (begin == std::string::npos) {
                return false;
            }
            const auto end = source.find(");", begin);
            return end != std::string::npos &&
                   source.substr(begin, end - begin).find("log_context") != std::string::npos;
        }

        TEST(TrashLogContextContractTest, ControllerDtoAndServiceUseExplicitRequestContext) {
            const auto controller_source = ReadSourceFile("src/controllers/TrashController.cpp");
            const auto dto_source = ReadSourceFile("src/dtos/TrashDto.hpp");
            const auto service_source = ReadSourceFile("src/services/TrashService.cpp");

            const auto& controller_body = controller_source;
            const auto service_body = ExtractRange(
                service_source,
                "auto TrashService::List(",
                "    auto TrashService::ExtractExtension("
            );

            ASSERT_FALSE(controller_body.empty());
            ASSERT_FALSE(dto_source.empty());
            ASSERT_FALSE(service_body.empty());
            EXPECT_EQ(
                CountOccurrences(
                    controller_body,
                    "GetRequestLogContext(request, \"trash\")"
                ),
                4
            );
            EXPECT_TRUE(CallContainsContext(
                controller_body,
                "TrashListRequest::FromRequest("
            ));
            EXPECT_EQ(
                CountOccurrences(
                    controller_body,
                    "TrashBatchRequest::FromRequest(request, log_context)"
                ),
                2
            );
            for (const auto* call_marker : {
                     "m_trash_service->List(",
                     "m_trash_service->Count(",
                     "m_trash_service->Restore(",
                     "m_trash_service->Delete(",
                     "m_trash_service->DeleteAll(",
                 }) {
                EXPECT_TRUE(CallContainsContext(controller_body, call_marker));
            }

            EXPECT_EQ(CountOccurrences(dto_source, "disk::utils::LogContext log_context = {}"), 2);
            EXPECT_EQ(
                CountOccurrences(service_body, "disk::utils::LogContext log_context"),
                10
            );
            for (const auto* call_marker : {
                     "co_await RestoreFile(",
                     "co_await RestoreFolder(",
                     "co_await PermanentlyDeleteTrashItems(",
                 }) {
                EXPECT_TRUE(CallContainsContext(service_body, call_marker));
            }
            EXPECT_TRUE(Contains(
                service_body,
                "DecrementRefCountsAndEnqueueGc("
            ));
            for (const auto* obsolete_helper : {
                     "auto TrashService::GenerateUniqueFilename(",
                     "auto TrashService::IsFilenameExists(",
                     "auto TrashService::IsFolderNameExists(",
                     "auto TrashService::IsFolderExists(",
                     "auto TrashService::RestoreFile(\n        uint64_t trash_id,",
                     "auto TrashService::RestoreFolder(\n        uint64_t trash_id,",
                     "auto TrashService::DeleteFile(\n        uint64_t trash_id,",
                     "auto TrashService::DeleteFolder(\n        uint64_t trash_id,",
                 }) {
                EXPECT_FALSE(Contains(service_source, obsolete_helper));
            }
            EXPECT_FALSE(Contains(service_source, "auto TrashService::UpdateStorageUsed("));

            for (const auto* body : { &controller_body, &dto_source, &service_body }) {
                EXPECT_FALSE(Contains(*body, "Logger::Debug()"));
                EXPECT_FALSE(Contains(*body, "Logger::Info()"));
                EXPECT_FALSE(Contains(*body, "Logger::Warn()"));
                EXPECT_FALSE(Contains(*body, "Logger::Error()"));
                EXPECT_FALSE(Contains(*body, "LOG_DEBUG"));
                EXPECT_FALSE(Contains(*body, "LOG_INFO"));
                EXPECT_FALSE(Contains(*body, "LOG_WARN"));
                EXPECT_FALSE(Contains(*body, "LOG_ERROR"));
            }
        }

    } // namespace
} // namespace disk::trash
