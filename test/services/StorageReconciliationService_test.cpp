#include "services/StorageReconciliationService.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace disk::reconciliation {
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

        auto CountOccurrences(const std::string& source, std::string_view expected) -> size_t {
            size_t count = 0;
            size_t position = 0;
            while ((position = source.find(expected, position)) != std::string::npos) {
                ++count;
                position += expected.size();
            }
            return count;
        }

        TEST(StorageReconciliationContractTest, ParsesAllStableScopes) {
            EXPECT_EQ(ParseReconciliationScope("contents"), ReconciliationScope::Contents);
            EXPECT_EQ(ParseReconciliationScope("users"), ReconciliationScope::Users);
            EXPECT_EQ(ParseReconciliationScope("staging"), ReconciliationScope::Staging);
            EXPECT_EQ(ParseReconciliationScope("final"), ReconciliationScope::Final);
            EXPECT_FALSE(ParseReconciliationScope("unknown").has_value());
        }

        TEST(StorageReconciliationContractTest, ValidatesScopeSpecificCursorsAndBounds) {
            ReconciliationPageRequest database_request{
                .scan_id = "2026-07-19T12:00Z",
                .scope = ReconciliationScope::Contents,
                .after_id = 7,
                .limit = kMaxDatabaseReconciliationPageSize,
            };
            EXPECT_TRUE(ValidateReconciliationPageRequest(database_request).has_value());

            database_request.continuation_token = "opaque";
            EXPECT_FALSE(ValidateReconciliationPageRequest(database_request).has_value());

            ReconciliationPageRequest object_request{
                .scan_id = "scan_123",
                .scope = ReconciliationScope::Staging,
                .continuation_token = "opaque-token",
                .limit = kMaxObjectReconciliationPageSize,
            };
            EXPECT_TRUE(ValidateReconciliationPageRequest(object_request).has_value());

            object_request.after_id = 1;
            EXPECT_FALSE(ValidateReconciliationPageRequest(object_request).has_value());
            object_request.after_id = 0;
            object_request.limit = kMaxObjectReconciliationPageSize + 1;
            EXPECT_FALSE(ValidateReconciliationPageRequest(object_request).has_value());
        }

        TEST(StorageReconciliationContractTest, ObjectResourceIdsAreStableAndBounded) {
            const auto first = BuildObjectResourceId("staging/upload/chunks/0.part");
            const auto repeated = BuildObjectResourceId("staging/upload/chunks/0.part");
            const auto different = BuildObjectResourceId("staging/upload/chunks/1.part");

            EXPECT_EQ(first.size(), 64U);
            EXPECT_EQ(first, repeated);
            EXPECT_NE(first, different);
        }

        TEST(StorageReconciliationContractTest, OnlyProvenZeroReferencesAllowAutomaticGc) {
            EXPECT_TRUE(ShouldEnqueueBlobGc(0, 0));
            EXPECT_FALSE(ShouldEnqueueBlobGc(1, 0));
            EXPECT_FALSE(ShouldEnqueueBlobGc(0, 1));
            EXPECT_FALSE(ShouldEnqueueBlobGc(-1, 0));
        }

        TEST(StorageReconciliationContractTest, FailureLogsAreFixedAndRedacted) {
            const auto source =
                ReadSourceFile("src/services/StorageReconciliationService.cpp");

            ASSERT_FALSE(source.empty());
            EXPECT_EQ(CountOccurrences(source, ".what()"), 0U);
            EXPECT_EQ(
                CountOccurrences(
                    source,
                    "Logger::Warn(log_context) << \"Storage reconciliation database failure\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    source,
                    "Logger::Warn(log_context) << \"Storage reconciliation failed\";"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    source,
                    "ErrorInfo(ErrorCode::InternalError, \"Storage reconciliation database failure\")"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    source,
                    "ErrorInfo(ErrorCode::InternalError, \"Storage reconciliation failed\")"
                ),
                1U
            );
        }
    } // namespace
} // namespace disk::reconciliation
