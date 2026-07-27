/**
 * @file DownloadResponder_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief DownloadResponder 与 RangeRequest::Parse 特征测试
 *
 * @details
 * 捕获当前下载行为作为重构前的基线。包含：
 * - RangeRequest::Parse 的纯函数测试（8 个用例）
 * - BuildDownloadResponse 的集成行为测试（descriptor/stream/sendfile/error 分支）
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "../../src/controllers/DownloadResponder.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include <drogon/HttpResponse.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>
#include <json/json.h>

#include "../../src/dtos/FileDto.hpp"
#include "../../src/services/DownloadIntegrityService.hpp"
#include "../../src/storage/IBlobStore.hpp"
#include "../../src/utils/ErrorCode.hpp"
#include "../../src/utils/Response.hpp"

namespace {

    constexpr uint64_t TEST_CONTENT_ID = 1;
    constexpr std::string_view TEST_BLOB_HASH = "abc123hash";
    constexpr std::string_view TEST_BLOB_PATH = "legacy/ab/persisted-object.bin";

    /// ============================================================
    /// 辅助：解析 JSON body
    /// ============================================================
    auto ParseJsonBody(const drogon::HttpResponsePtr& resp) -> Json::Value {
        Json::Value root;
        Json::CharReaderBuilder builder;
        const auto body = resp->getBody();
        auto body_str = std::string(body);
        std::istringstream stream(body_str);
        Json::parseFromStream(builder, stream, &root, nullptr);
        return root;
    }

    class MockFileReadStream final : public disk::storage::StorageReadStream {
    public:
        MockFileReadStream(std::ifstream stream, uint64_t remaining)
            : m_stream(std::move(stream)), m_remaining(remaining) {}

        auto Read(char* buffer, std::size_t length) -> std::size_t override {
            if (buffer == nullptr || !m_stream.is_open() || m_remaining == 0) {
                return 0;
            }
            const auto read_size = static_cast<std::size_t>(std::min<uint64_t>(
                std::min<uint64_t>(length, m_remaining),
                static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max())
            ));
            m_stream.read(buffer, static_cast<std::streamsize>(read_size));
            const auto read_bytes = static_cast<std::size_t>(m_stream.gcount());
            m_remaining -= read_bytes;
            if (read_bytes == 0 || m_remaining == 0) {
                Close();
            }
            return read_bytes;
        }

        auto Close() -> void override {
            if (m_stream.is_open()) {
                m_stream.close();
            }
        }

    private:
        std::ifstream m_stream;
        uint64_t m_remaining{ 0 };
    };

    /// ============================================================
    /// MockBlobStore — 仅实现下载读取所需行为，其他返回默认成功值
    /// ============================================================
    class MockBlobStore final : public disk::storage::IBlobStore {
    public:
        explicit MockBlobStore(std::filesystem::path temp_dir)
            : m_temp_dir(std::move(temp_dir)) {}

        ~MockBlobStore() override {
            std::error_code ec;
            std::filesystem::remove_all(m_temp_dir, ec);
        }

        /// ---- 测试辅助 ----

        auto CreateBlob(const disk::storage::BlobDescriptor& blob, const std::string& content)
            -> std::filesystem::path {
            auto path = std::filesystem::path(blob.storage_path);
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            std::ofstream ofs(path, std::ios::binary);
            ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
            ofs.close();
            m_blob_paths[blob.storage_path] = path;
            return path;
        }

        auto ResetCounters() -> void {
            open_blob_count = 0;
            blob_exists_count = 0;
            local_path_count = 0;
        }

        /// ---- IBlobStore 接口 ----

        auto PromoteToFinal(
            const disk::storage::UploadStagingAssembly& /*assembly*/,
            const std::string& /*hash*/,
            disk::utils::LogContext /*log_context*/
        ) -> drogon::Task<Result<disk::storage::BlobPromoteResult>> override {
            co_return disk::storage::BlobPromoteResult{ .path = m_temp_dir / "final", .created = true };
        }

        auto OpenBlobRangeForRead(
            const disk::storage::BlobDescriptor& blob,
            uint64_t start,
            uint64_t length,
            disk::utils::LogContext /*log_context*/
        ) -> drogon::Task<Result<std::shared_ptr<disk::storage::StorageReadStream>>> override {
            ++open_blob_count;
            if (open_blob_should_fail) {
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::FileReadError, "Failed to open blob for reading")
                );
            }
            auto path_it = m_blob_paths.find(blob.storage_path);
            if (path_it == m_blob_paths.end()) {
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::FileReadError, "Failed to open blob for reading")
                );
            }
            if (start > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max())) {
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Invalid request range")
                );
            }

            std::ifstream stream(path_it->second, std::ios::binary);
            if (!stream) {
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::FileReadError, "Failed to open file for reading")
                );
            }
            stream.seekg(static_cast<std::streamoff>(start));
            if (!stream) {
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::FileReadError, "Failed to seek blob for reading")
                );
            }
            co_return std::shared_ptr<disk::storage::StorageReadStream>(
                std::make_shared<MockFileReadStream>(std::move(stream), length)
            );
        }

        auto DeleteBlob(
            const std::filesystem::path& /*storage_path*/,
            disk::utils::LogContext /*log_context*/
        )
            -> drogon::Task<Result<void>> override {
            co_return Result<void>{};
        }

        auto BlobExists(
            const disk::storage::BlobDescriptor& /*blob*/,
            disk::utils::LogContext /*log_context*/
        )
            -> drogon::Task<Result<bool>> override {
            ++blob_exists_count;
            co_return blob_exists;
        }

        auto GetLocalBlobPathForDownload(const disk::storage::BlobDescriptor& blob) const
            -> std::optional<std::filesystem::path> override {
            ++local_path_count;
            if (!local_path_available) {
                return std::nullopt;
            }
            auto path_it = m_blob_paths.find(blob.storage_path);
            if (path_it == m_blob_paths.end()) {
                return std::nullopt;
            }
            return path_it->second;
        }

        auto GetFinalStoragePath(const std::string& hash) const
            -> std::filesystem::path override {
            return m_temp_dir / (hash + ".bin");
        }

        auto GetFileSize(
            const std::filesystem::path& /*storage_path*/,
            disk::utils::LogContext /*log_context*/
        )
            -> drogon::Task<Result<uint64_t>> override {
            co_return static_cast<uint64_t>(0);
        }

        bool blob_exists{ true };
        bool local_path_available{ true };
        bool open_blob_should_fail{ false };
        mutable int local_path_count{ 0 };
        int open_blob_count{ 0 };
        int blob_exists_count{ 0 };

    private:
        std::filesystem::path m_temp_dir;
        std::unordered_map<std::string, std::filesystem::path> m_blob_paths;
    };

    class MockDownloadIntegrityService final
        : public disk::download::IDownloadIntegrityService {
    public:
        auto Preflight(
            const disk::storage::BlobDescriptor& blob,
            uint64_t expected_size,
            disk::utils::LogContext log_context
        ) -> drogon::Task<Result<void>> override {
            ++preflight_count;
            last_content_id = blob.content_id;
            last_expected_bytes = expected_size;
            preflight_log_context = std::move(log_context);
            co_return preflight_result;
        }

        auto RecordOpenFailure(
            const disk::storage::BlobDescriptor& blob,
            ErrorCode error_code,
            uint64_t range_start,
            uint64_t expected_bytes,
            disk::utils::LogContext log_context
        ) -> drogon::Task<void> override {
            ++open_failure_count;
            last_content_id = blob.content_id;
            last_error_code = error_code;
            last_range_start = range_start;
            last_expected_bytes = expected_bytes;
            open_failure_log_context = std::move(log_context);
            co_return;
        }

        auto RecordStreamInterruption(
            const disk::storage::BlobDescriptor& blob,
            uint64_t range_start,
            uint64_t expected_bytes,
            uint64_t delivered_bytes,
            disk::utils::LogContext log_context
        ) noexcept -> void override {
            ++stream_interruption_count;
            last_content_id = blob.content_id;
            last_range_start = range_start;
            last_expected_bytes = expected_bytes;
            last_delivered_bytes = delivered_bytes;
            stream_interruption_log_context = std::move(log_context);
        }

        Result<void> preflight_result{};
        int preflight_count{ 0 };
        int open_failure_count{ 0 };
        int stream_interruption_count{ 0 };
        uint64_t last_content_id{ 0 };
        uint64_t last_range_start{ 0 };
        uint64_t last_expected_bytes{ 0 };
        uint64_t last_delivered_bytes{ 0 };
        ErrorCode last_error_code{ ErrorCode::Success };
        disk::utils::LogContext preflight_log_context;
        disk::utils::LogContext open_failure_log_context;
        disk::utils::LogContext stream_interruption_log_context;
    };

    /// ============================================================
    /// RAII 临时目录
    /// ============================================================
    class TempDirGuard {
    public:
        TempDirGuard() {
            auto temp = std::filesystem::temp_directory_path() / "disk_test_download_XXXXXX";
            auto templ = temp.string();
            if (::mkdtemp(templ.data()) != nullptr) {
                m_path = templ;
            }
        }

        ~TempDirGuard() {
            if (!m_path.empty()) {
                std::error_code ec;
                std::filesystem::remove_all(m_path, ec);
            }
        }

        [[nodiscard]] auto Path() const -> const std::filesystem::path& { return m_path; }

        TempDirGuard(const TempDirGuard&) = delete;
        auto operator=(const TempDirGuard&) -> TempDirGuard& = delete;

    private:
        std::filesystem::path m_path;
    };

} // namespace

/// ================================================================
/// Suite 1: RangeRequest::Parse 纯函数测试
/// ================================================================
TEST(RangeRequestParseTest, EmptyHeaderReturnsNoRange) {
    auto result = disk::file::RangeRequest::Parse("", 1000);
    EXPECT_FALSE(result.has_range);
    EXPECT_TRUE(result.satisfiable);
}

TEST(RangeRequestParseTest, NormalRange) {
    auto result = disk::file::RangeRequest::Parse("bytes=0-499", 1000);
    EXPECT_TRUE(result.has_range);
    EXPECT_TRUE(result.satisfiable);
    EXPECT_EQ(result.start, 0U);
    EXPECT_EQ(result.end, 499U);
}

TEST(RangeRequestParseTest, OpenEndRange) {
    auto result = disk::file::RangeRequest::Parse("bytes=500-", 1000);
    EXPECT_TRUE(result.has_range);
    EXPECT_TRUE(result.satisfiable);
    EXPECT_EQ(result.start, 500U);
    EXPECT_EQ(result.end, 999U);
}

TEST(RangeRequestParseTest, SuffixRange) {
    auto result = disk::file::RangeRequest::Parse("bytes=-500", 1000);
    EXPECT_TRUE(result.has_range);
    EXPECT_TRUE(result.satisfiable);
    EXPECT_EQ(result.start, 500U);
    EXPECT_EQ(result.end, 999U);
}

TEST(RangeRequestParseTest, SuffixRangeExceedsFileSize) {
    auto result = disk::file::RangeRequest::Parse("bytes=-500", 300);
    EXPECT_TRUE(result.has_range);
    EXPECT_TRUE(result.satisfiable);
    EXPECT_EQ(result.start, 0U);
    EXPECT_EQ(result.end, 299U);
}

TEST(RangeRequestParseTest, StartExceedsFileSize) {
    auto result = disk::file::RangeRequest::Parse("bytes=999999-", 1000);
    EXPECT_TRUE(result.has_range);
    EXPECT_FALSE(result.satisfiable);
}

TEST(RangeRequestParseTest, StartGreaterThanEnd) {
    auto result = disk::file::RangeRequest::Parse("bytes=300-200", 1000);
    EXPECT_TRUE(result.has_range);
    EXPECT_FALSE(result.satisfiable);
}

TEST(RangeRequestParseTest, InvalidPrefix) {
    auto result = disk::file::RangeRequest::Parse("invalid", 1000);
    EXPECT_TRUE(result.has_range);
    EXPECT_FALSE(result.satisfiable);
}

/// ================================================================
/// Suite 2: BuildDownloadResponse 行为测试
/// ================================================================
class DownloadResponderTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_temp_dir = std::make_unique<TempDirGuard>();
        m_storage = std::make_unique<MockBlobStore>(m_temp_dir->Path());
        m_integrity = std::make_unique<MockDownloadIntegrityService>();
    }

    void TearDown() override {
        m_integrity.reset();
        m_storage.reset();
        m_temp_dir.reset();
    }

    /// 创建一个 file_size 字节的测试文件，内容为重复的模式字节
    auto MakeTestFile(uint64_t file_size) -> std::string {
        std::string content(file_size, '\0');
        for (uint64_t i = 0; i < file_size; ++i) {
            content[i] = static_cast<char>('0' + (i % 10));
        }
        return content;
    }

    /// 创建临时 Blob 并返回 DownloadParams
    auto MakeDownloadParams(
        const std::string& content,
        const std::string& range_header = ""
    ) -> disk::controllers::DownloadParams {
        auto blob = disk::storage::BlobDescriptor{
            .content_id = TEST_CONTENT_ID,
            .hash_md5 = std::string(TEST_BLOB_HASH),
            .storage_path = (m_temp_dir->Path() / TEST_BLOB_PATH).string(),
            .size = content.size()
        };
        m_storage->CreateBlob(blob, content);
        disk::controllers::DownloadParams params;
        params.blob = blob;
        params.filename = "testfile.bin";
        params.file_size = content.size();
        params.mime_type = "application/octet-stream";
        params.file_hash = std::string(TEST_BLOB_HASH);
        params.range_header = range_header;
        return params;
    }

    std::unique_ptr<TempDirGuard> m_temp_dir;
    std::unique_ptr<MockBlobStore> m_storage;
    std::unique_ptr<MockDownloadIntegrityService> m_integrity;
};

TEST_F(DownloadResponderTest, FullDownloadReturns200) {
    auto content = MakeTestFile(1000);
    auto params = MakeDownloadParams(content);

    auto resp = drogon::sync_wait(
        disk::controllers::BuildDownloadResponse(params, m_storage.get(), m_integrity.get())
    );

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::HttpStatusCode::k200OK);
    EXPECT_EQ(resp->getHeader("Content-Length"), std::to_string(content.size()));
    EXPECT_EQ(resp->getHeader("Accept-Ranges"), "bytes");
    EXPECT_FALSE(resp->getHeader("Content-Disposition").empty());
    EXPECT_EQ(resp->getHeader("ETag"), "\"abc123hash\"");
    EXPECT_EQ(m_storage->open_blob_count, 1);
    EXPECT_EQ(m_storage->blob_exists_count, 0);
    EXPECT_EQ(m_storage->local_path_count, 0);
    EXPECT_EQ(m_integrity->preflight_count, 1);

    /// newStreamResponse body is only populated when sent through HTTP;
    /// header verification captures the current behavior characterization.
    EXPECT_TRUE(resp->getBody().empty());
}

TEST_F(DownloadResponderTest, RangeRequestReturns206) {
    auto content = MakeTestFile(1000);
    auto params = MakeDownloadParams(content, "bytes=0-499");

    auto resp = drogon::sync_wait(
        disk::controllers::BuildDownloadResponse(params, m_storage.get(), m_integrity.get())
    );

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::HttpStatusCode::k206PartialContent);
    EXPECT_EQ(resp->getHeader("Content-Range"), "bytes 0-499/1000");
    EXPECT_EQ(resp->getHeader("Content-Length"), "500");
    EXPECT_EQ(resp->getHeader("Accept-Ranges"), "bytes");
    EXPECT_EQ(m_storage->open_blob_count, 1);
    EXPECT_TRUE(resp->getBody().empty());
}

TEST_F(DownloadResponderTest, OpenEndRangeReturns206) {
    auto content = MakeTestFile(1000);
    auto params = MakeDownloadParams(content, "bytes=500-");

    auto resp = drogon::sync_wait(
        disk::controllers::BuildDownloadResponse(params, m_storage.get(), m_integrity.get())
    );

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::HttpStatusCode::k206PartialContent);
    EXPECT_EQ(resp->getHeader("Content-Range"), "bytes 500-999/1000");
    EXPECT_EQ(resp->getHeader("Content-Length"), "500");
    EXPECT_EQ(m_storage->open_blob_count, 1);
    EXPECT_TRUE(resp->getBody().empty());
}

TEST_F(DownloadResponderTest, SuffixRangeReturns206) {
    auto content = MakeTestFile(1000);
    auto params = MakeDownloadParams(content, "bytes=-500");

    auto resp = drogon::sync_wait(
        disk::controllers::BuildDownloadResponse(params, m_storage.get(), m_integrity.get())
    );

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::HttpStatusCode::k206PartialContent);
    EXPECT_EQ(resp->getHeader("Content-Range"), "bytes 500-999/1000");
    EXPECT_EQ(resp->getHeader("Content-Length"), "500");
    EXPECT_EQ(m_storage->open_blob_count, 1);
    EXPECT_TRUE(resp->getBody().empty());
}

TEST_F(DownloadResponderTest, InvalidRangeReturns416WithoutTouchingStorage) {
    auto content = MakeTestFile(1000);
    auto params = MakeDownloadParams(content, "bytes=999999-");
    m_storage->ResetCounters();

    auto resp = drogon::sync_wait(
        disk::controllers::BuildDownloadResponse(params, m_storage.get(), m_integrity.get())
    );

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(
        resp->getStatusCode(),
        drogon::HttpStatusCode::k416RequestedRangeNotSatisfiable
    );
    EXPECT_EQ(resp->getHeader("Content-Range"), "bytes */1000");
    EXPECT_EQ(m_storage->open_blob_count, 0);
    EXPECT_EQ(m_storage->blob_exists_count, 0);
    EXPECT_EQ(m_storage->local_path_count, 0);
    EXPECT_EQ(m_integrity->preflight_count, 0);

    auto json = ParseJsonBody(resp);
    EXPECT_EQ(json["code"].asInt(), 10002);
    EXPECT_EQ(json["message"].asString(), "Invalid request range");
    EXPECT_TRUE(json.isMember("data"));
    EXPECT_EQ(json["data"]["file_size"].asUInt64(), 1000U);
}

TEST_F(DownloadResponderTest, LargeLocalFileUsesLocalFileResponse) {
    auto content = MakeTestFile(300 * 1024);
    auto params = MakeDownloadParams(content);

    auto resp = drogon::sync_wait(
        disk::controllers::BuildDownloadResponse(params, m_storage.get(), m_integrity.get())
    );

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::HttpStatusCode::k200OK);
    EXPECT_EQ(resp->getHeader("Accept-Ranges"), "bytes");
    EXPECT_EQ(resp->getHeader("ETag"), "\"abc123hash\"");
    EXPECT_EQ(m_storage->blob_exists_count, 0);
    EXPECT_EQ(m_storage->local_path_count, 1);
    EXPECT_EQ(m_storage->open_blob_count, 0);
}

TEST_F(DownloadResponderTest, LargeRangeLocalFileUsesLocalFileResponse) {
    auto content = MakeTestFile(300 * 1024);
    auto params = MakeDownloadParams(content, "bytes=0-262143");

    auto resp = drogon::sync_wait(
        disk::controllers::BuildDownloadResponse(params, m_storage.get(), m_integrity.get())
    );

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::HttpStatusCode::k206PartialContent);
    EXPECT_EQ(resp->getHeader("Accept-Ranges"), "bytes");
    EXPECT_EQ(m_storage->blob_exists_count, 0);
    EXPECT_EQ(m_storage->local_path_count, 1);
    EXPECT_EQ(m_storage->open_blob_count, 0);
}

TEST_F(DownloadResponderTest, LargeFileWithoutLocalPathFallsBackToStream) {
    auto content = MakeTestFile(300 * 1024);
    auto params = MakeDownloadParams(content);
    m_storage->local_path_available = false;

    auto resp = drogon::sync_wait(
        disk::controllers::BuildDownloadResponse(params, m_storage.get(), m_integrity.get())
    );

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::HttpStatusCode::k200OK);
    EXPECT_EQ(resp->getHeader("Content-Length"), std::to_string(content.size()));
    EXPECT_EQ(resp->getHeader("Accept-Ranges"), "bytes");
    EXPECT_EQ(m_storage->blob_exists_count, 0);
    EXPECT_EQ(m_storage->local_path_count, 1);
    EXPECT_EQ(m_storage->open_blob_count, 1);
}

TEST_F(DownloadResponderTest, PreflightFailureReturns500WithoutOpeningStorage) {
    auto content = MakeTestFile(300 * 1024);
    auto params = MakeDownloadParams(content);
    const disk::utils::LogContext log_context{
        .request_id = "download-preflight-request",
        .operation = "download",
    };
    m_integrity->preflight_result =
        std::unexpected(ErrorInfo(ErrorCode::FileReadError));

    auto resp = drogon::sync_wait(
        disk::controllers::BuildDownloadResponse(
            params,
            m_storage.get(),
            m_integrity.get(),
            log_context
        )
    );

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::HttpStatusCode::k500InternalServerError);
    EXPECT_EQ(ParseJsonBody(resp)["code"].asUInt(), 50011U);
    EXPECT_EQ(m_integrity->preflight_count, 1);
    EXPECT_EQ(m_storage->blob_exists_count, 0);
    EXPECT_EQ(m_storage->local_path_count, 0);
    EXPECT_EQ(m_storage->open_blob_count, 0);
    EXPECT_EQ(
        m_integrity->preflight_log_context.request_id,
        log_context.request_id
    );
    EXPECT_EQ(
        m_integrity->preflight_log_context.operation,
        log_context.operation
    );
}

TEST_F(DownloadResponderTest, OpenBlobFailureReturns500AndRecordsFinding) {
    auto content = MakeTestFile(1000);
    auto params = MakeDownloadParams(content);
    const disk::utils::LogContext log_context{
        .request_id = "download-open-request",
        .operation = "download",
    };
    m_storage->open_blob_should_fail = true;

    auto resp = drogon::sync_wait(
        disk::controllers::BuildDownloadResponse(
            params,
            m_storage.get(),
            m_integrity.get(),
            log_context
        )
    );

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::HttpStatusCode::k500InternalServerError);
    EXPECT_EQ(ParseJsonBody(resp)["code"].asUInt(), 50011U);
    EXPECT_EQ(m_storage->open_blob_count, 1);
    EXPECT_EQ(m_integrity->open_failure_count, 1);
    EXPECT_EQ(m_integrity->last_error_code, ErrorCode::FileReadError);
    EXPECT_EQ(m_integrity->last_content_id, TEST_CONTENT_ID);
    EXPECT_EQ(m_integrity->last_range_start, 0U);
    EXPECT_EQ(m_integrity->last_expected_bytes, content.size());
    EXPECT_EQ(
        m_integrity->open_failure_log_context.request_id,
        log_context.request_id
    );
    EXPECT_EQ(
        m_integrity->open_failure_log_context.operation,
        log_context.operation
    );
}

TEST_F(DownloadResponderTest, StreamShortReadRecordsDeliveredBytesOnce) {
    auto content = MakeTestFile(100);
    auto params = MakeDownloadParams(content);
    const disk::utils::LogContext log_context{
        .request_id = "download-stream-request",
        .operation = "download",
    };
    params.file_size = 200;
    params.blob.size = 200;

    auto resp = drogon::sync_wait(
        disk::controllers::BuildDownloadResponse(
            params,
            m_storage.get(),
            m_integrity.get(),
            log_context
        )
    );

    ASSERT_NE(resp, nullptr);
    ASSERT_EQ(resp->getStatusCode(), drogon::HttpStatusCode::k200OK);
    auto& callback = resp->streamCallback();
    std::array<char, 256> buffer{};

    EXPECT_EQ(callback(buffer.data(), buffer.size()), content.size());
    EXPECT_EQ(callback(buffer.data(), buffer.size()), 0U);
    EXPECT_EQ(callback(buffer.data(), buffer.size()), 0U);
    EXPECT_EQ(m_integrity->stream_interruption_count, 1);
    EXPECT_EQ(m_integrity->last_content_id, TEST_CONTENT_ID);
    EXPECT_EQ(m_integrity->last_range_start, 0U);
    EXPECT_EQ(m_integrity->last_expected_bytes, 200U);
    EXPECT_EQ(m_integrity->last_delivered_bytes, content.size());
    EXPECT_EQ(
        m_integrity->stream_interruption_log_context.request_id,
        log_context.request_id
    );
    EXPECT_EQ(
        m_integrity->stream_interruption_log_context.operation,
        log_context.operation
    );
}
