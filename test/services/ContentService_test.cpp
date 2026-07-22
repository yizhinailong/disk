#include "services/ContentService.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace disk::content {
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

        auto Contains(const std::string& source, const std::string& expected) -> bool {
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
                    source.substr(position, end - position).find("log_context") ==
                        std::string::npos) {
                    return false;
                }
                ++count;
                position = end + 2;
            }
            return count == expected_count;
        }

        TEST(ContentServiceCompileTest, CanConstructWithNullDbClient) {
            ContentService service(nullptr);
            SUCCEED();
        }

        TEST(ContentServiceContractTest, NewContentCarriesImmutableBlobMetadata) {
            NewContent content;
            content.hash_md5 = "0123456789abcdef0123456789abcdef";
            content.hash_sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
            content.size = 42;
            content.storage_path = "build/uploaded/01/blob.bin";

            EXPECT_EQ(content.size, 42U);
            EXPECT_EQ(content.hash_md5.size(), 32U);
            EXPECT_EQ(content.hash_sha256.size(), 64U);
        }

        TEST(ContentServiceContractTest, OwnsFileContentsLifecycleBoundary) {
            /// file_contents lookup, creation, ref-count mutation, and zero-ref verification are
            /// intentionally exposed together so callers keep DB changes transaction-aware.
            using FindDefaultResult = decltype(std::declval<ContentService&>().FindByMd5(
                std::declval<const std::string&>()
            ));
            using FindWithClientResult = decltype(std::declval<ContentService&>().FindByMd5(
                std::declval<const drogon::orm::DbClientPtr&>(),
                std::declval<const std::string&>()
            ));
            using FindExistingIdsResult = decltype(std::declval<ContentService&>().FindExistingIds(
                std::declval<const drogon::orm::DbClientPtr&>(),
                std::declval<const std::vector<uint64_t>&>()
            ));
            using AcquireReferenceResult = decltype(std::declval<ContentService&>().AcquireReference(
                std::declval<const drogon::orm::DbClientPtr&>(),
                std::declval<const NewContent&>(),
                std::declval<std::optional<uint64_t>>()
            ));
            using IncrementResult = decltype(std::declval<ContentService&>().IncrementRefCount(
                std::declval<const drogon::orm::DbClientPtr&>(),
                uint64_t{ 1 },
                uint64_t{ 1 }
            ));
            using BatchIncrementResult = decltype(std::declval<ContentService&>().IncrementRefCounts(
                std::declval<const drogon::orm::DbClientPtr&>(),
                std::declval<const std::unordered_map<uint64_t, uint64_t>&>(),
                std::declval<const std::unordered_set<uint64_t>&>()
            ));
            using CheckedBatchIncrementResult = decltype(std::declval<ContentService&>().IncrementRefCountsChecked(
                std::declval<const drogon::orm::DbClientPtr&>(),
                std::declval<const std::unordered_map<uint64_t, uint64_t>&>(),
                std::declval<const std::unordered_set<uint64_t>&>()
            ));
            using DecrementResult = decltype(std::declval<ContentService&>().DecrementRefCountsAndEnqueueGc(
                std::declval<const drogon::orm::DbClientPtr&>(),
                std::declval<const std::unordered_map<uint64_t, uint64_t>&>()
            ));

            EXPECT_TRUE((std::is_same_v<FindDefaultResult, drogon::Task<std::optional<ContentMetadata>>>));
            EXPECT_TRUE((std::is_same_v<FindWithClientResult, drogon::Task<std::optional<ContentMetadata>>>));
            EXPECT_TRUE((std::is_same_v<FindExistingIdsResult, drogon::Task<std::unordered_set<uint64_t>>>));
            EXPECT_TRUE((std::is_same_v<AcquireReferenceResult, drogon::Task<Result<ContentMetadata>>>));
            EXPECT_TRUE((std::is_same_v<IncrementResult, drogon::Task<Result<void>>>));
            EXPECT_TRUE((std::is_same_v<BatchIncrementResult, drogon::Task<std::unordered_set<uint64_t>>>));
            EXPECT_TRUE((std::is_same_v<CheckedBatchIncrementResult, drogon::Task<Result<std::unordered_set<uint64_t>>>>));
            EXPECT_TRUE((std::is_same_v<DecrementResult, drogon::Task<Result<size_t>>>));
        }

        TEST(ContentServiceLogContextContractTest, SharedLifecycleFailuresKeepCallerContext) {
            const auto header = ReadSourceFile("src/services/ContentService.hpp");
            const auto source = ReadSourceFile("src/services/ContentService.cpp");
            const auto upload_lifecycle =
                ReadSourceFile("src/services/UploadLifecycleService.cpp");
            const auto file_mutation = ReadSourceFile("src/services/FileMutationService.cpp");
            const auto share = ReadSourceFile("src/services/ShareService.cpp");
            const auto trash = ReadSourceFile("src/services/TrashService.cpp");

            ASSERT_FALSE(header.empty());
            ASSERT_FALSE(source.empty());
            ASSERT_FALSE(upload_lifecycle.empty());
            ASSERT_FALSE(file_mutation.empty());
            ASSERT_FALSE(share.empty());
            ASSERT_FALSE(trash.empty());

            EXPECT_EQ(
                CountOccurrences(header, "disk::utils::LogContext log_context = {}"),
                8U
            );
            EXPECT_EQ(CountOccurrences(source, "Logger::Warn(log_context)"), 8U);
            EXPECT_EQ(CountOccurrences(source, "Logger::Error(log_context)"), 2U);
            EXPECT_EQ(CountOccurrences(source, "Logger::Debug()"), 1U);
            EXPECT_FALSE(Contains(source, "Logger::Warn()"));
            EXPECT_FALSE(Contains(source, "Logger::Error()"));

            for (const auto message : {
                     "\"File content lookup failed\"",
                     "\"File content batch ID lookup failed\"",
                     "\"File content reference acquisition failed\"",
                     "\"File content reference increment failed\"",
                     "\"File content batch reference increment rejected\"",
                     "\"File content batch reference increment row mismatch\"",
                     "\"File content batch reference increment failed\"",
                     "\"File content decrement and Blob GC enqueue failed\"",
                     "\"Blob GC enqueue failed\"",
                     "\"Blob GC reference gate failed\"",
                 }) {
                EXPECT_TRUE(Contains(source, message));
            }

            EXPECT_TRUE(EveryCallContainsContext(
                source,
                "co_return co_await FindByMd5(",
                1U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                source,
                "co_await IncrementRefCountsChecked(",
                1U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                source,
                "co_await CheckReferenceGate(",
                4U
            ));

            EXPECT_TRUE(EveryCallContainsContext(
                upload_lifecycle,
                "content_service.FindByMd5(",
                2U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                upload_lifecycle,
                "content_service.IncrementRefCount(",
                1U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                upload_lifecycle,
                "content_service.AcquireReference(",
                1U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                file_mutation,
                "content_service.FindExistingIds(",
                2U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                file_mutation,
                "content_service.IncrementRefCountsChecked(",
                2U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                share,
                "content_service.IncrementRefCount(",
                2U
            ));
            EXPECT_TRUE(EveryCallContainsContext(
                trash,
                "content_service.DecrementRefCountsAndEnqueueGc(",
                1U
            ));

            EXPECT_FALSE(Contains(source, "log_context."));
            EXPECT_FALSE(Contains(source, ".what()"));
            EXPECT_FALSE(Contains(source, "result.error().message"));
            EXPECT_FALSE(Contains(source, "content_id="));
            EXPECT_FALSE(Contains(source, "Authorization"));
            EXPECT_FALSE(Contains(source, "Failed to find file content by md5:"));
            EXPECT_FALSE(Contains(source, "affected unexpected rows"));
        }

    } // namespace
} // namespace disk::content
