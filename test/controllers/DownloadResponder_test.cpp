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

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include <drogon/HttpResponse.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>
#include <json/json.h>

#include "../../src/dtos/FileDto.hpp"
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
            open_path_count = 0;
            exists_path_count = 0;
        }

        /// ---- IBlobStore 接口 ----

        auto PromoteToFinal(
            const disk::storage::UploadStagingAssembly& /*assembly*/,
            const std::string& /*hash*/
        ) -> drogon::Task<Result<disk::storage::BlobPromoteResult>> override {
            co_return disk::storage::BlobPromoteResult{ .path = m_temp_dir / "final", .created = true };
        }

        auto OpenForRead(const std::filesystem::path& storage_path)
            -> drogon::Task<Result<std::shared_ptr<std::ifstream>>> override {
            ++open_path_count;
            co_return OpenPath(storage_path);
        }

        auto OpenBlobForRead(const disk::storage::BlobDescriptor& blob)
            -> drogon::Task<Result<std::shared_ptr<std::ifstream>>> override {
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
            co_return OpenPath(path_it->second);
        }

        auto DeleteBlob(const std::filesystem::path& /*storage_path*/)
            -> drogon::Task<Result<void>> override {
            co_return Result<void>{};
        }

        auto Exists(const std::filesystem::path& /*storage_path*/)
            -> drogon::Task<Result<bool>> override {
            ++exists_path_count;
            co_return true;
        }

        auto BlobExists(const disk::storage::BlobDescriptor& /*blob*/)
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

        auto GetFileSize(const std::filesystem::path& /*storage_path*/)
            -> drogon::Task<Result<uint64_t>> override {
            co_return static_cast<uint64_t>(0);
        }

        bool blob_exists{ true };
        bool local_path_available{ true };
        bool open_blob_should_fail{ false };
        mutable int local_path_count{ 0 };
        int open_blob_count{ 0 };
        int blob_exists_count{ 0 };
        int open_path_count{ 0 };
        int exists_path_count{ 0 };

    private:
        [[nodiscard]]
        static auto OpenPath(const std::filesystem::path& path)
            -> Result<std::shared_ptr<std::ifstream>> {
            auto stream = std::make_shared<std::ifstream>(path, std::ios::binary);
            if (!*stream) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::FileReadError, "Failed to open file for reading")
                );
            }
            return stream;
        }

        std::filesystem::path m_temp_dir;
        std::unordered_map<std::string, std::filesystem::path> m_blob_paths;
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
    }

    void TearDown() override {
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
};

TEST_F(DownloadResponderTest, FullDownloadReturns200) {
    auto content = MakeTestFile(1000);
    auto params = MakeDownloadParams(content);

    auto resp = drogon::sync_wait(
        disk::controllers::BuildDownloadResponse(params, m_storage.get())
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

    /// newStreamResponse body is only populated when sent through HTTP;
    /// header verification captures the current behavior characterization.
    EXPECT_TRUE(resp->getBody().empty());
}

TEST_F(DownloadResponderTest, RangeRequestReturns206) {
    auto content = MakeTestFile(1000);
    auto params = MakeDownloadParams(content, "bytes=0-499");

    auto resp = drogon::sync_wait(
        disk::controllers::BuildDownloadResponse(params, m_storage.get())
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
        disk::controllers::BuildDownloadResponse(params, m_storage.get())
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
        disk::controllers::BuildDownloadResponse(params, m_storage.get())
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
        disk::controllers::BuildDownloadResponse(params, m_storage.get())
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
    EXPECT_EQ(m_storage->open_path_count, 0);
    EXPECT_EQ(m_storage->exists_path_count, 0);

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
        disk::controllers::BuildDownloadResponse(params, m_storage.get())
    );

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::HttpStatusCode::k200OK);
    EXPECT_EQ(resp->getHeader("Accept-Ranges"), "bytes");
    EXPECT_EQ(resp->getHeader("ETag"), "\"abc123hash\"");
    EXPECT_EQ(m_storage->blob_exists_count, 1);
    EXPECT_EQ(m_storage->local_path_count, 1);
    EXPECT_EQ(m_storage->open_blob_count, 0);
}

TEST_F(DownloadResponderTest, LargeRangeLocalFileUsesLocalFileResponse) {
    auto content = MakeTestFile(300 * 1024);
    auto params = MakeDownloadParams(content, "bytes=0-262143");

    auto resp = drogon::sync_wait(
        disk::controllers::BuildDownloadResponse(params, m_storage.get())
    );

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::HttpStatusCode::k206PartialContent);
    EXPECT_EQ(resp->getHeader("Accept-Ranges"), "bytes");
    EXPECT_EQ(m_storage->blob_exists_count, 1);
    EXPECT_EQ(m_storage->local_path_count, 1);
    EXPECT_EQ(m_storage->open_blob_count, 0);
}

TEST_F(DownloadResponderTest, LargeFileWithoutLocalPathFallsBackToStream) {
    auto content = MakeTestFile(300 * 1024);
    auto params = MakeDownloadParams(content);
    m_storage->local_path_available = false;

    auto resp = drogon::sync_wait(
        disk::controllers::BuildDownloadResponse(params, m_storage.get())
    );

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::HttpStatusCode::k200OK);
    EXPECT_EQ(resp->getHeader("Content-Length"), std::to_string(content.size()));
    EXPECT_EQ(resp->getHeader("Accept-Ranges"), "bytes");
    EXPECT_EQ(m_storage->blob_exists_count, 1);
    EXPECT_EQ(m_storage->local_path_count, 1);
    EXPECT_EQ(m_storage->open_blob_count, 1);
}

TEST_F(DownloadResponderTest, MissingLargeBlobReturns404) {
    auto content = MakeTestFile(300 * 1024);
    auto params = MakeDownloadParams(content);
    m_storage->blob_exists = false;

    auto resp = drogon::sync_wait(
        disk::controllers::BuildDownloadResponse(params, m_storage.get())
    );

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::HttpStatusCode::k404NotFound);
    EXPECT_EQ(m_storage->blob_exists_count, 1);
    EXPECT_EQ(m_storage->local_path_count, 0);
    EXPECT_EQ(m_storage->open_blob_count, 0);
}

TEST_F(DownloadResponderTest, OpenBlobFailureReturns404) {
    auto content = MakeTestFile(1000);
    auto params = MakeDownloadParams(content);
    m_storage->open_blob_should_fail = true;

    auto resp = drogon::sync_wait(
        disk::controllers::BuildDownloadResponse(params, m_storage.get())
    );

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::HttpStatusCode::k404NotFound);
    EXPECT_EQ(m_storage->open_blob_count, 1);
}
