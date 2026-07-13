/**
 * @file DownloadResponder_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief DownloadResponder 与 RangeRequest::Parse 特征测试
 *
 * @details
 * 捕获当前下载行为作为重构前的基线。包含：
 * - RangeRequest::Parse 的纯函数测试（8 个用例）
 * - BuildDownloadResponse 的集成行为测试（5 个用例）
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "../../src/controllers/DownloadResponder.hpp"
#include "../../src/dtos/FileDto.hpp"
#include "../../src/storage/IFileStorage.hpp"
#include "../../src/utils/ErrorCode.hpp"
#include "../../src/utils/Response.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <drogon/HttpResponse.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>
#include <json/json.h>

namespace {

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
    /// MockBlobStore — 仅实现下载所需的 BlobStore 读取行为
    /// ============================================================
    class MockBlobStore final : public disk::storage::BlobStore {
    public:
        explicit MockBlobStore(std::filesystem::path temp_dir)
            : m_temp_dir(std::move(temp_dir)) {}

        ~MockBlobStore() override {
            std::error_code ec;
            std::filesystem::remove_all(m_temp_dir, ec);
        }

        /// ---- 测试辅助 ----

        /// 在临时目录创建文件并写入 content，返回该文件路径
        auto CreateTempFile(const std::string& name, const std::string& content)
            -> std::filesystem::path {
            auto path = m_temp_dir / name;
            std::ofstream ofs(path, std::ios::binary);
            ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
            ofs.close();
            return path;
        }

        /// ---- BlobStore 接口 ----

        auto PromoteToFinal(
            const std::filesystem::path& /*temp_path*/,
            const std::string& /*hash*/
        ) -> drogon::Task<Result<disk::storage::PromoteResult>> override {
            co_return disk::storage::PromoteResult{ .path = m_temp_dir / "final", .created = true };
        }

        auto OpenForRead(const std::filesystem::path& storage_path)
            -> drogon::Task<Result<std::shared_ptr<std::ifstream>>> override {
            auto stream = std::make_shared<std::ifstream>(storage_path, std::ios::binary);
            if (!*stream) {
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::FileReadError, "Failed to open file for reading")
                );
            }
            co_return stream;
        }

        auto DeletePath(const std::filesystem::path& /*target_path*/)
            -> drogon::Task<Result<void>> override {
            co_return Result<void>{};
        }

        auto Exists(const std::filesystem::path& /*target_path*/)
            -> drogon::Task<Result<bool>> override {
            co_return true;
        }

        auto GetFinalStoragePath(const std::string& /*hash*/) const
            -> std::filesystem::path override {
            return m_temp_dir / "final";
        }

        auto GetFileSize(const std::filesystem::path& /*target_path*/)
            -> drogon::Task<Result<uint64_t>> override {
            co_return static_cast<uint64_t>(0);
        }

    private:
        std::filesystem::path m_temp_dir;
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

} ///< namespace

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
        m_blob_store = std::make_unique<MockBlobStore>(m_temp_dir->Path());
    }

    void TearDown() override {
        m_blob_store.reset();
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

    /// 创建临时文件并返回 DownloadParams
    auto MakeDownloadParams(
        const std::string& content,
        const std::string& range_header = ""
    ) -> disk::controllers::DownloadParams {
        auto path = m_blob_store->CreateTempFile("testfile.bin", content);
        disk::controllers::DownloadParams params;
        params.storage_path = path.string();
        params.filename = "testfile.bin";
        params.file_size = content.size();
        params.mime_type = "application/octet-stream";
        params.file_hash = "abc123hash";
        params.range_header = range_header;
        return params;
    }

    std::unique_ptr<TempDirGuard> m_temp_dir;
    std::unique_ptr<MockBlobStore> m_blob_store;
};

TEST_F(DownloadResponderTest, FullDownloadReturns200) {
    auto content = MakeTestFile(1000);
    auto params = MakeDownloadParams(content);

    auto resp = drogon::sync_wait(
        disk::controllers::BuildDownloadResponse(params, m_blob_store.get())
    );

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::HttpStatusCode::k200OK);
    EXPECT_EQ(resp->getHeader("Content-Length"), std::to_string(content.size()));
    EXPECT_EQ(resp->getHeader("Accept-Ranges"), "bytes");
    EXPECT_FALSE(resp->getHeader("Content-Disposition").empty());
    EXPECT_EQ(resp->getHeader("ETag"), "\"abc123hash\"");

    /// newStreamResponse body is only populated when sent through HTTP;
    /// header verification captures the current behavior characterization.
    EXPECT_TRUE(resp->getBody().empty());
}

TEST_F(DownloadResponderTest, RangeRequestReturns206) {
    auto content = MakeTestFile(1000);
    auto params = MakeDownloadParams(content, "bytes=0-499");

    auto resp = drogon::sync_wait(
        disk::controllers::BuildDownloadResponse(params, m_blob_store.get())
    );

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::HttpStatusCode::k206PartialContent);
    EXPECT_EQ(resp->getHeader("Content-Range"), "bytes 0-499/1000");
    EXPECT_EQ(resp->getHeader("Content-Length"), "500");
    EXPECT_EQ(resp->getHeader("Accept-Ranges"), "bytes");
    EXPECT_TRUE(resp->getBody().empty());
}

TEST_F(DownloadResponderTest, OpenEndRangeReturns206) {
    auto content = MakeTestFile(1000);
    auto params = MakeDownloadParams(content, "bytes=500-");

    auto resp = drogon::sync_wait(
        disk::controllers::BuildDownloadResponse(params, m_blob_store.get())
    );

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::HttpStatusCode::k206PartialContent);
    EXPECT_EQ(resp->getHeader("Content-Range"), "bytes 500-999/1000");
    EXPECT_EQ(resp->getHeader("Content-Length"), "500");
    EXPECT_TRUE(resp->getBody().empty());
}

TEST_F(DownloadResponderTest, SuffixRangeReturns206) {
    auto content = MakeTestFile(1000);
    auto params = MakeDownloadParams(content, "bytes=-500");

    auto resp = drogon::sync_wait(
        disk::controllers::BuildDownloadResponse(params, m_blob_store.get())
    );

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->getStatusCode(), drogon::HttpStatusCode::k206PartialContent);
    EXPECT_EQ(resp->getHeader("Content-Range"), "bytes 500-999/1000");
    EXPECT_EQ(resp->getHeader("Content-Length"), "500");
    EXPECT_TRUE(resp->getBody().empty());
}

TEST_F(DownloadResponderTest, InvalidRangeReturns416) {
    auto content = MakeTestFile(1000);
    auto params = MakeDownloadParams(content, "bytes=999999-");

    auto resp = drogon::sync_wait(
        disk::controllers::BuildDownloadResponse(params, m_blob_store.get())
    );

    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(
        resp->getStatusCode(),
        drogon::HttpStatusCode::k416RequestedRangeNotSatisfiable
    );
    EXPECT_EQ(resp->getHeader("Content-Range"), "bytes */1000");

    auto json = ParseJsonBody(resp);
    EXPECT_EQ(json["code"].asInt(), 10002);
    EXPECT_EQ(json["message"].asString(), "Invalid request range");
    EXPECT_TRUE(json.isMember("data"));
    EXPECT_EQ(json["data"]["file_size"].asUInt64(), 1000U);
}
