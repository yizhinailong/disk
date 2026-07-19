#include "storage/S3ObjectStorage.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <drogon/drogon.h>
#include <gtest/gtest.h>
#include <json/json.h>

#include "utils/ConfigMgr.hpp"

namespace {

    class MemoryReadStream final : public disk::storage::StorageReadStream {
    public:
        explicit MemoryReadStream(std::string data) : m_data(std::move(data)) {}

        auto Read(char* buffer, std::size_t length) -> std::size_t override {
            if (buffer == nullptr || m_offset >= m_data.size()) {
                return 0;
            }
            const auto bytes = std::min(length, m_data.size() - m_offset);
            std::copy_n(m_data.data() + m_offset, bytes, buffer);
            m_offset += bytes;
            return bytes;
        }

        auto Close() -> void override { m_offset = m_data.size(); }

    private:
        std::string m_data;
        std::size_t m_offset{ 0 };
    };

    class FakeS3Client final : public disk::storage::IS3Client {
    public:
        auto ValidateBucketAccessible() -> Result<void> override { return {}; }

        auto HeadObject(const std::string& key) -> Result<disk::storage::S3HeadObjectResult> override {
            ++head_calls;
            const auto it = objects.find(key);
            if (it == objects.end()) {
                return disk::storage::S3HeadObjectResult{ .exists = false, .size = 0, .etag = {} };
            }
            return disk::storage::S3HeadObjectResult{
                .exists = true,
                .size = static_cast<uint64_t>(it->second.size()),
                .etag = EtagFor(it->second)
            };
        }

        auto PutObject(const std::string& key, std::string data)
            -> Result<disk::storage::S3PutObjectResult> override {
            ++put_calls;
            uploaded_keys.push_back(key);
            const auto etag = EtagFor(data);
            objects[key] = std::move(data);
            return disk::storage::S3PutObjectResult{ .etag = etag };
        }

        auto PutObjectFromFile(const std::string& key, const std::filesystem::path& local_path)
            -> Result<void> override {
            ++put_calls;
            uploaded_keys.push_back(key);
            if (read_local_file) {
                std::ifstream input(local_path, std::ios::binary);
                if (input) {
                    objects[key] = std::string(
                        std::istreambuf_iterator<char>(input),
                        std::istreambuf_iterator<char>()
                    );
                } else {
                    objects[key] = uploaded_fallback_content;
                }
            } else {
                objects[key] = uploaded_fallback_content;
            }
            return {};
        }

        auto DeleteObject(const std::string& key) -> Result<void> override {
            ++delete_calls;
            deleted_keys.push_back(key);
            if (delete_failures_remaining > 0) {
                --delete_failures_remaining;
                return std::unexpected(delete_error);
            }
            objects.erase(key);
            return {};
        }

        auto GetObjectRange(const std::string& key, uint64_t start, uint64_t length)
            -> Result<std::shared_ptr<disk::storage::StorageReadStream>> override {
            ++get_calls;
            last_range_key = key;
            last_range_start = start;
            last_range_length = length;
            const auto it = objects.find(key);
            if (it == objects.end()) {
                return std::unexpected(ErrorInfo(ErrorCode::FileNotFound, "fake object not found"));
            }
            const auto bounded_start = std::min<uint64_t>(start, it->second.size());
            const auto bounded_length = std::min<uint64_t>(length, it->second.size() - bounded_start);
            return std::shared_ptr<disk::storage::StorageReadStream>(
                std::make_shared<MemoryReadStream>(it->second.substr(bounded_start, bounded_length))
            );
        }

        auto ListObjects(
            const std::string& prefix,
            const std::string& continuation_token,
            uint32_t max_keys
        ) -> Result<disk::storage::S3ListObjectsResult> override {
            disk::storage::S3ListObjectsResult result;
            auto it = continuation_token.empty() ? objects.lower_bound(prefix) : objects.upper_bound(continuation_token);
            for (; it != objects.end() && it->first.starts_with(prefix); ++it) {
                if (result.keys.size() == max_keys) {
                    result.is_truncated = true;
                    result.continuation_token = result.keys.back();
                    break;
                }
                result.keys.push_back(it->first);
            }
            return result;
        }

        auto DeleteObjects(const std::vector<std::string>& keys) -> Result<void> override {
            for (const auto& key : keys) {
                objects.erase(key);
                deleted_keys.push_back(key);
            }
            return {};
        }

        auto CreateMultipartUpload(const std::string& key) -> Result<std::string> override {
            const auto upload_id = "multipart-" + std::to_string(++next_multipart_id);
            multipart_uploads.emplace(upload_id, MultipartUpload{ .key = key });
            return upload_id;
        }

        auto UploadPart(
            const std::string& key,
            const std::string& upload_id,
            int part_number,
            std::string data
        ) -> Result<std::string> override {
            auto upload = multipart_uploads.find(upload_id);
            if (upload == multipart_uploads.end() || upload->second.key != key) {
                return std::unexpected(ErrorInfo(ErrorCode::InternalError, "fake multipart not found"));
            }
            const auto etag = EtagFor(data);
            upload->second.parts[part_number] = std::move(data);
            return etag;
        }

        auto UploadPartCopy(
            const std::string& source_key,
            const std::string& destination_key,
            const std::string& upload_id,
            int part_number,
            uint64_t start,
            uint64_t length
        ) -> Result<std::string> override {
            const auto source = objects.find(source_key);
            if (source == objects.end() || start > source->second.size()) {
                return std::unexpected(ErrorInfo(ErrorCode::FileNotFound, "fake copy source not found"));
            }
            return UploadPart(
                destination_key,
                upload_id,
                part_number,
                source->second.substr(static_cast<size_t>(start), static_cast<size_t>(length))
            );
        }

        auto CompleteMultipartUpload(
            const std::string& key,
            const std::string& upload_id,
            const std::vector<disk::storage::S3CompletedPart>& parts
        ) -> Result<void> override {
            auto upload = multipart_uploads.find(upload_id);
            if (upload == multipart_uploads.end() || upload->second.key != key) {
                return std::unexpected(ErrorInfo(ErrorCode::InternalError, "fake multipart not found"));
            }

            std::string content;
            for (const auto& part : parts) {
                const auto stored_part = upload->second.parts.find(part.part_number);
                if (stored_part == upload->second.parts.end() || EtagFor(stored_part->second) != part.etag) {
                    return std::unexpected(ErrorInfo(ErrorCode::InternalError, "fake multipart part mismatch"));
                }
                content += stored_part->second;
            }
            objects[key] = std::move(content);
            multipart_uploads.erase(upload);
            return {};
        }

        auto AbortMultipartUpload(const std::string& /*key*/, const std::string& upload_id)
            -> Result<void> override {
            multipart_uploads.erase(upload_id);
            return {};
        }

        struct MultipartUpload {
            std::string key;
            std::map<int, std::string> parts;
        };

        static auto EtagFor(const std::string& data) -> std::string {
            return "\"fake-etag-" + std::to_string(data.size()) + "\"";
        }

        std::map<std::string, std::string> objects;
        std::string uploaded_fallback_content{ "fallback" };
        bool read_local_file{ true };
        std::vector<std::string> uploaded_keys;
        std::vector<std::string> deleted_keys;
        std::string last_range_key;
        uint64_t last_range_start{ 0 };
        uint64_t last_range_length{ 0 };
        int head_calls{ 0 };
        int put_calls{ 0 };
        int delete_calls{ 0 };
        int get_calls{ 0 };
        int delete_failures_remaining{ 0 };
        int next_multipart_id{ 0 };
        std::map<std::string, MultipartUpload> multipart_uploads;
        ErrorInfo delete_error{ ErrorCode::InternalError, "fake delete failure" };
    };

    auto LoadS3StorageConfig(const std::filesystem::path& temp_upload_path) -> void {
        Json::Value cfg;
        auto& disk = cfg["custom_config"]["disk"];
        disk["storage_backend"] = "s3";
        disk["temp_upload_path"] = temp_upload_path.string();
        disk["assembly_max_concurrent"] = 4;
        disk["assemble_buffer_size_bytes"] = 4096;
        disk["s3"]["bucket"] = "disk-test";
        disk["s3"]["region"] = "us-east-1";
        disk["s3"]["object_prefix"] = "objects";
        drogon::app().loadConfigJson(cfg);
        disk::utils::ConfigMgr::GetInstance()->LoadConfig();
    }

    class S3ObjectStorageTest : public ::testing::Test {
    protected:
        void SetUp() override {
            const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
            root = std::filesystem::path("build/test_s3_object_storage") /
                   (std::string(info->test_suite_name()) + "_" + info->name());
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
            std::filesystem::create_directories(root, ec);
            LoadS3StorageConfig(root / "temp");
            client = std::make_shared<FakeS3Client>();
            storage = std::make_unique<disk::storage::S3ObjectStorage>(
                disk::utils::ConfigMgr::GetInstance(),
                client
            );
        }

        void TearDown() override {
            storage.reset();
            Json::Value cfg;
            cfg["custom_config"]["disk"]["storage_backend"] = "local";
            cfg["custom_config"]["disk"]["assembly_max_concurrent"] = 4;
            cfg["custom_config"]["disk"]["assemble_buffer_size_bytes"] = 1048576;
            drogon::app().loadConfigJson(cfg);
            disk::utils::ConfigMgr::GetInstance()->LoadConfig();
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }

        auto WriteTempFile(const std::string& name, const std::string& content) -> std::filesystem::path {
            const auto path = root / name;
            std::ofstream output(path, std::ios::binary);
            output << content;
            return path;
        }

        std::filesystem::path root;
        std::shared_ptr<FakeS3Client> client;
        std::unique_ptr<disk::storage::S3ObjectStorage> storage;
    };

    auto LocalAssembly(const std::filesystem::path& path) -> disk::storage::UploadStagingAssembly {
        return disk::storage::UploadStagingAssembly{
            .backend = disk::storage::UploadStagingBackend::Local,
            .locator = path.string(),
        };
    }

} // namespace

TEST_F(S3ObjectStorageTest, GetFinalStoragePathUsesHashShardedObjectKey) {
    EXPECT_EQ(
        storage->GetFinalStoragePath("abcdef0123456789abcdef0123456789").generic_string(),
        "objects/ab/abcdef0123456789abcdef0123456789.bin"
    );
}

TEST_F(S3ObjectStorageTest, PromoteToFinalUploadsMissingObject) {
    const auto temp_path = WriteTempFile("assembled.tmp", "hello s3");

    auto result = drogon::sync_wait(
        storage->PromoteToFinal(LocalAssembly(temp_path), "abcdef0123456789abcdef0123456789")
    );

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_TRUE(result->created);
    EXPECT_EQ(result->path.generic_string(), "objects/ab/abcdef0123456789abcdef0123456789.bin");
    EXPECT_EQ(client->put_calls, 1);
    EXPECT_EQ(client->objects["objects/ab/abcdef0123456789abcdef0123456789.bin"], "hello s3");
    EXPECT_FALSE(std::filesystem::exists(temp_path));
}

TEST_F(S3ObjectStorageTest, PromoteToFinalReusesExistingObject) {
    client->objects["objects/ab/abcdef0123456789abcdef0123456789.bin"] = "existing";
    const auto temp_path = WriteTempFile("assembled.tmp", "new bytes");

    auto result = drogon::sync_wait(
        storage->PromoteToFinal(LocalAssembly(temp_path), "abcdef0123456789abcdef0123456789")
    );

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FALSE(result->created);
    EXPECT_EQ(client->put_calls, 0);
    EXPECT_EQ(client->objects["objects/ab/abcdef0123456789abcdef0123456789.bin"], "existing");
    EXPECT_FALSE(std::filesystem::exists(temp_path));
}

TEST_F(S3ObjectStorageTest, PromoteToFinalSucceedsWhenTempCleanupFailsAfterUpload) {
    const auto temp_path = root / "non_empty_dir.tmp";
    std::filesystem::create_directories(temp_path);
    WriteTempFile("non_empty_dir.tmp/child", "child");
    client->read_local_file = false;

    auto result = drogon::sync_wait(
        storage->PromoteToFinal(LocalAssembly(temp_path), "abcdef0123456789abcdef0123456789")
    );

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_TRUE(result->created);
    EXPECT_EQ(client->put_calls, 1);
    EXPECT_TRUE(std::filesystem::exists(temp_path));
}

TEST_F(S3ObjectStorageTest, ExistsSizeRangeAndDeleteUseObjectKey) {
    client->objects["objects/ab/abcdef0123456789abcdef0123456789.bin"] = "0123456789";
    const auto key = std::filesystem::path("objects/ab/abcdef0123456789abcdef0123456789.bin");

    auto exists = drogon::sync_wait(storage->Exists(key));
    ASSERT_TRUE(exists.has_value()) << exists.error().message;
    EXPECT_TRUE(*exists);

    auto size = drogon::sync_wait(storage->GetFileSize(key));
    ASSERT_TRUE(size.has_value()) << size.error().message;
    EXPECT_EQ(*size, 10U);

    auto stream_result = drogon::sync_wait(storage->OpenBlobRangeForRead(
        disk::storage::BlobDescriptor{ .content_id = 1, .hash_md5 = "abcdef0123456789abcdef0123456789", .size = 10 },
        2,
        4
    ));
    ASSERT_TRUE(stream_result.has_value()) << stream_result.error().message;
    char buffer[8]{};
    EXPECT_EQ((*stream_result)->Read(buffer, 8), 4U);
    EXPECT_EQ(std::string(buffer, 4), "2345");
    EXPECT_EQ(client->last_range_key, key.generic_string());
    EXPECT_EQ(client->last_range_start, 2U);
    EXPECT_EQ(client->last_range_length, 4U);

    auto delete_result = drogon::sync_wait(storage->DeleteBlob(key));
    ASSERT_TRUE(delete_result.has_value()) << delete_result.error().message;
    EXPECT_EQ(client->delete_calls, 1);
    EXPECT_FALSE(client->objects.contains(key.generic_string()));
}

TEST_F(S3ObjectStorageTest, DeleteBlobRetriesTransientFailures) {
    const auto key = std::filesystem::path("objects/ab/abcdef0123456789abcdef0123456789.bin");
    client->objects[key.generic_string()] = "payload";
    client->delete_failures_remaining = 2;

    auto result = drogon::sync_wait(storage->DeleteBlob(key));

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(client->delete_calls, 3);
    EXPECT_FALSE(client->objects.contains(key.generic_string()));
}

TEST_F(S3ObjectStorageTest, DeleteBlobStopsAfterThreeFailuresAndReturnsLastError) {
    const auto key = std::filesystem::path("objects/ab/abcdef0123456789abcdef0123456789.bin");
    client->objects[key.generic_string()] = "payload";
    client->delete_failures_remaining = 10;
    client->delete_error = ErrorInfo(ErrorCode::InternalError, "persistent fake delete failure");

    auto result = drogon::sync_wait(storage->DeleteBlob(key));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InternalError);
    EXPECT_EQ(result.error().message, "persistent fake delete failure");
    EXPECT_EQ(client->delete_calls, 3);
    EXPECT_TRUE(client->objects.contains(key.generic_string()));
}

TEST_F(S3ObjectStorageTest, DeleteBlobIsIdempotentForMissingObject) {
    const auto key = std::filesystem::path("objects/ab/abcdef0123456789abcdef0123456789.bin");

    auto first_result = drogon::sync_wait(storage->DeleteBlob(key));
    auto second_result = drogon::sync_wait(storage->DeleteBlob(key));

    ASSERT_TRUE(first_result.has_value()) << first_result.error().message;
    ASSERT_TRUE(second_result.has_value()) << second_result.error().message;
    EXPECT_EQ(client->delete_calls, 2);
}
