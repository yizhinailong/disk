#include "services/QuotaService.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

namespace disk::quota {
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

        auto StreamsIdentifier(const std::string& source, std::string_view identifier) -> bool {
            size_t position = 0;
            while ((position = source.find("<<", position)) != std::string::npos) {
                auto cursor = position + 2;
                while (cursor < source.size() &&
                       std::isspace(static_cast<unsigned char>(source[cursor])) != 0) {
                    ++cursor;
                }
                const auto end = cursor + identifier.size();
                if (end <= source.size() &&
                    source.compare(cursor, identifier.size(), identifier) == 0 &&
                    (end == source.size() ||
                     (std::isalnum(static_cast<unsigned char>(source[end])) == 0 &&
                      source[end] != '_'))) {
                    return true;
                }
                position = cursor;
            }
            return false;
        }

        TEST(QuotaServiceCompileTest, CanConstructWithNullDbClient) {
            QuotaService service(nullptr);
            SUCCEED();
        }

        TEST(QuotaServiceContractTest, ExposesGenericReservationForTransactions) {
            using ReserveResult = decltype(std::declval<QuotaService&>().ReserveStorage(
                std::declval<const drogon::orm::DbClientPtr&>(),
                uint64_t{ 1 },
                uint64_t{ 1 }
            ));

            EXPECT_TRUE((std::is_same_v<ReserveResult, drogon::Task<Result<void>>>));
        }

        TEST(QuotaServiceContractTest, ExposesCheckedUsedStorageAdjustmentForTransactions) {
            using CheckedAdjustResult = decltype(std::declval<QuotaService&>().AdjustUsedStorageChecked(
                std::declval<const drogon::orm::DbClientPtr&>(),
                uint64_t{ 1 },
                int64_t{ -1 }
            ));

            EXPECT_TRUE((std::is_same_v<CheckedAdjustResult, drogon::Task<Result<void>>>));
        }

        TEST(QuotaServiceContractTest, ReconciliationCarriesPersistedAndObservedAccounting) {
            AccountingReconciliation reconciliation;
            reconciliation.user_id = 7;
            reconciliation.storage_used = 100;
            reconciliation.storage_reserved = 20;
            reconciliation.storage_quota = 1000;
            reconciliation.active_file_bytes = 60;
            reconciliation.trash_item_bytes = 40;
            reconciliation.in_progress_reserved_bytes = 20;

            EXPECT_EQ(reconciliation.user_id, 7u);
            EXPECT_EQ(reconciliation.storage_used, 100u);
            EXPECT_EQ(reconciliation.storage_reserved, 20u);
            EXPECT_EQ(reconciliation.storage_quota, 1000u);
            EXPECT_EQ(reconciliation.active_file_bytes, 60u);
            EXPECT_EQ(reconciliation.trash_item_bytes, 40u);
            EXPECT_EQ(reconciliation.in_progress_reserved_bytes, 20u);
        }

        TEST(QuotaServiceContractTest, PropagatesTypedLogContextWithoutInference) {
            const auto header = ReadSourceFile("src/services/QuotaService.hpp");
            const auto source = ReadSourceFile("src/services/QuotaService.cpp");
            const auto upload_lifecycle =
                ReadSourceFile("src/services/UploadLifecycleService.cpp");
            const auto file_mutation_header =
                ReadSourceFile("src/services/FileMutationService.hpp");
            const auto file_mutation =
                ReadSourceFile("src/services/FileMutationService.cpp");
            const auto trash_header = ReadSourceFile("src/services/TrashService.hpp");
            const auto trash = ReadSourceFile("src/services/TrashService.cpp");
            const auto cleanup_header = ReadSourceFile("src/services/CleanupService.hpp");
            const auto cleanup = ReadSourceFile("src/services/CleanupService.cpp");

            ASSERT_FALSE(header.empty());
            ASSERT_FALSE(source.empty());
            ASSERT_FALSE(upload_lifecycle.empty());
            ASSERT_FALSE(file_mutation.empty());
            ASSERT_FALSE(trash.empty());
            ASSERT_FALSE(cleanup.empty());

            EXPECT_EQ(
                CountOccurrences(header, "disk::utils::LogContext log_context = {}"),
                13U
            );
            EXPECT_EQ(CountOccurrences(source, "Logger::Debug(log_context)"), 5U);
            EXPECT_EQ(CountOccurrences(source, "Logger::Warn(log_context)"), 4U);
            EXPECT_EQ(CountOccurrences(source, "Logger::Error(log_context)"), 9U);
            EXPECT_EQ(CountOccurrences(source, "Logger::Debug()"), 1U);
            EXPECT_FALSE(Contains(source, "Logger::Warn()"));
            EXPECT_FALSE(Contains(source, "Logger::Error()"));

            EXPECT_TRUE(EveryCallContainsContext(
                source,
                "co_return co_await ReserveStorage(",
                3U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                source,
                "co_await ReleaseReservedStorage(",
                1U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                source,
                "co_await ReleaseReservedStorageChecked(",
                1U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                source,
                "co_await AdjustUsedStorageChecked(",
                1U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                source,
                "co_return co_await GetReconciliation(",
                1U
            ));

            EXPECT_TRUE(EveryCallContainsContext(
                upload_lifecycle,
                "quota_service.",
                8U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                file_mutation,
                "quota_service.",
                4U
            ));
            EXPECT_TRUE(EveryCallContainsContext(trash, "quota_service.", 1U));

            EXPECT_FALSE(Contains(source, "log_context."));
            EXPECT_FALSE(Contains(source, ".what()"));
            EXPECT_FALSE(Contains(source, "result.error().message"));
            EXPECT_FALSE(StreamsIdentifier(source, "user_id"));
            EXPECT_FALSE(StreamsIdentifier(source, "bytes"));
            EXPECT_FALSE(StreamsIdentifier(source, "delta"));
            EXPECT_FALSE(Contains(source, "Authorization"));

            EXPECT_TRUE(Contains(
                upload_lifecycle,
                "Logger::Warn(log_context) << \"Upload storage quota reservation failed\";"
            ));
            EXPECT_TRUE(Contains(
                file_mutation,
                "Logger::Warn(log_context) << \"Copy storage quota reservation failed\";"
            ));
            EXPECT_FALSE(Contains(file_mutation_header, "CheckStorageQuota("));
            EXPECT_FALSE(Contains(file_mutation_header, "UpdateStorageUsed("));
            EXPECT_FALSE(Contains(trash_header, "UpdateStorageUsed("));
            EXPECT_FALSE(Contains(cleanup_header, "UpdateStorageUsed("));
            EXPECT_FALSE(Contains(cleanup, "QuotaService"));
        }

    } // namespace
} // namespace disk::quota
