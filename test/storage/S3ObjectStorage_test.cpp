#include "storage/S3ObjectStorage.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <drogon/drogon.h>
#include <gtest/gtest.h>
#include <json/json.h>

#include "utils/ConfigMgr.hpp"
#include "utils/FileHashUtil.hpp"

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

        auto PutObjectIfAbsent(const std::string& key, std::string data)
            -> Result<disk::storage::S3PutObjectResult> override {
            ++put_calls;
            uploaded_keys.push_back(key);
            if (objects.contains(key)) {
                return disk::storage::S3PutObjectResult{
                    .etag = {},
                    .created = false,
                };
            }
            const auto etag = EtagFor(data);
            objects[key] = std::move(data);
            return disk::storage::S3PutObjectResult{
                .etag = etag,
                .created = true,
            };
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
            auto bounded_length = std::min<uint64_t>(length, it->second.size() - bounded_start);
            if (truncate_next_range_by > 0) {
                bounded_length = bounded_length > truncate_next_range_by ? bounded_length - truncate_next_range_by : 0;
                truncate_next_range_by = 0;
            }
            return std::shared_ptr<disk::storage::StorageReadStream>(
                std::make_shared<MemoryReadStream>(it->second.substr(bounded_start, bounded_length))
            );
        }

        auto ListObjects(
            const std::string& prefix,
            const std::string& continuation_token,
            uint32_t max_keys
        ) -> Result<disk::storage::S3ListObjectsResult> override {
            ++list_calls;
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
            if (list_outside_prefix_key.has_value()) {
                result.keys.push_back(list_outside_prefix_key.value());
                list_outside_prefix_key.reset();
            }
            return result;
        }

        auto DeleteObjects(const std::vector<std::string>& keys) -> Result<void> override {
            ++delete_batch_calls;
            delete_batch_sizes.push_back(keys.size());
            for (const auto& key : keys) {
                objects.erase(key);
                deleted_keys.push_back(key);
            }
            return {};
        }

        auto CreateMultipartUpload(const std::string& key) -> Result<std::string> override {
            ++create_multipart_calls;
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
            if (upload_part_failure_number == part_number) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "fake upload part failure")
                );
            }
            uploaded_part_sizes.push_back(data.size());
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
            ++complete_multipart_calls;
            auto upload = multipart_uploads.find(upload_id);
            if (upload == multipart_uploads.end() || upload->second.key != key) {
                return std::unexpected(ErrorInfo(ErrorCode::InternalError, "fake multipart not found"));
            }
            if (complete_multipart_failure) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "fake multipart completion failure")
                );
            }

            std::string content;
            for (const auto& part : parts) {
                const auto stored_part = upload->second.parts.find(part.part_number);
                if (stored_part == upload->second.parts.end() || EtagFor(stored_part->second) != part.etag) {
                    return std::unexpected(ErrorInfo(ErrorCode::InternalError, "fake multipart part mismatch"));
                }
                completed_part_numbers.push_back(part.part_number);
                content += stored_part->second;
            }
            objects[key] = std::move(content);
            multipart_uploads.erase(upload);
            return {};
        }

        auto AbortMultipartUpload(const std::string& /*key*/, const std::string& upload_id)
            -> Result<void> override {
            ++abort_multipart_calls;
            aborted_upload_ids.push_back(upload_id);
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
        uint64_t truncate_next_range_by{ 0 };
        int head_calls{ 0 };
        int put_calls{ 0 };
        int delete_calls{ 0 };
        int get_calls{ 0 };
        int list_calls{ 0 };
        int delete_batch_calls{ 0 };
        int delete_failures_remaining{ 0 };
        int next_multipart_id{ 0 };
        int create_multipart_calls{ 0 };
        int complete_multipart_calls{ 0 };
        int abort_multipart_calls{ 0 };
        int upload_part_failure_number{ 0 };
        bool complete_multipart_failure{ false };
        std::optional<std::string> list_outside_prefix_key;
        std::vector<size_t> delete_batch_sizes;
        std::vector<size_t> uploaded_part_sizes;
        std::vector<int> completed_part_numbers;
        std::vector<std::string> aborted_upload_ids;
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

        [[nodiscard]] static auto S3Session(std::string upload_id = "upload-123")
            -> disk::storage::UploadStagingSession {
            return disk::storage::UploadStagingSession{
                .upload_id = upload_id,
                .backend = disk::storage::UploadStagingBackend::S3,
                .prefix = "staging/" + upload_id,
            };
        }

        auto WriteS3Chunk(
            const disk::storage::UploadStagingSession& session,
            uint32_t chunk_index,
            const std::string& data
        ) -> disk::storage::UploadStagingChunk {
            auto result = drogon::sync_wait(storage->WriteChunk(
                session,
                chunk_index,
                disk::utils::FileHashUtil::HashMd5(data),
                data
            ));
            if (!result) {
                ADD_FAILURE() << result.error().message;
                return {};
            }
            return result.value();
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

TEST_F(S3ObjectStorageTest, S3SessionValidationRejectsUnsafeOrMismatchedPrefixes) {
    auto valid_result = drogon::sync_wait(storage->EnsureUploadSession(S3Session()));
    ASSERT_TRUE(valid_result.has_value()) << valid_result.error().message;

    const std::vector<disk::storage::UploadStagingSession> invalid_sessions{
        { .upload_id = "upload-123",
         .backend = disk::storage::UploadStagingBackend::S3,
         .prefix = "staging/../upload-123"   },
        { .upload_id = "upload-123",
         .backend = disk::storage::UploadStagingBackend::S3,
         .prefix = "staging/another-upload"  },
        { .upload_id = "upload-123",
         .backend = disk::storage::UploadStagingBackend::S3,
         .prefix = "other-staging/upload-123" },
        { .upload_id = "upload-123",
         .backend = disk::storage::UploadStagingBackend::S3,
         .prefix = "staging\\upload-123"     },
    };
    for (const auto& session : invalid_sessions) {
        auto result = drogon::sync_wait(storage->EnsureUploadSession(session));
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    }
}

TEST_F(S3ObjectStorageTest, S3ChunkWriteIsConditionalAndIdempotent) {
    const auto session = S3Session();
    const std::string data = "immutable-s3-chunk";
    const auto md5_hash = disk::utils::FileHashUtil::HashMd5(data);
    const auto expected_key = "staging/upload-123/chunks/3-" + md5_hash + ".part";

    auto first = drogon::sync_wait(storage->WriteChunk(session, 3, md5_hash, data));
    auto repeated = drogon::sync_wait(storage->WriteChunk(session, 3, md5_hash, data));

    ASSERT_TRUE(first.has_value()) << first.error().message;
    ASSERT_TRUE(repeated.has_value()) << repeated.error().message;
    EXPECT_EQ(first->object_key, expected_key);
    EXPECT_EQ(first->etag, FakeS3Client::EtagFor(data));
    EXPECT_EQ(repeated->object_key, first->object_key);
    EXPECT_EQ(repeated->etag, first->etag);
    EXPECT_EQ(client->objects.at(expected_key), data);
    EXPECT_EQ(client->put_calls, 2);
    EXPECT_EQ(client->get_calls, 1);
}

TEST_F(S3ObjectStorageTest, S3ChunkWriteNeverOverwritesConflictingObject) {
    const auto session = S3Session();
    const std::string requested_data = "good";
    const std::string existing_data = "evil";
    const auto md5_hash = disk::utils::FileHashUtil::HashMd5(requested_data);
    const auto key = "staging/upload-123/chunks/0-" + md5_hash + ".part";
    client->objects[key] = existing_data;

    auto result = drogon::sync_wait(
        storage->WriteChunk(session, 0, md5_hash, requested_data)
    );

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::ChunkVerifyFailed);
    EXPECT_EQ(client->objects.at(key), existing_data);
    EXPECT_EQ(client->put_calls, 1);
    EXPECT_EQ(client->get_calls, 1);
}

TEST_F(S3ObjectStorageTest, S3ChunkWriteRejectsUnverifiedHashBeforePut) {
    const auto session = S3Session();

    auto result = drogon::sync_wait(storage->WriteChunk(
        session,
        0,
        "00000000000000000000000000000000",
        "payload"
    ));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::ChunkVerifyFailed);
    EXPECT_EQ(client->put_calls, 0);
    EXPECT_TRUE(client->objects.empty());
}

TEST_F(S3ObjectStorageTest, S3AssemblyStreamsChunksToVersionedMultipartObject) {
    const auto session = S3Session();
    const std::string first_data = "first-";
    const std::string second_data = "second";
    const auto first = WriteS3Chunk(session, 0, first_data);
    const auto second = WriteS3Chunk(session, 1, second_data);

    auto result = drogon::sync_wait(storage->AssembleChunks(
        session,
        7,
        2,
        std::vector{ first, second }
    ));

    const auto expected_data = first_data + second_data;
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->backend, disk::storage::UploadStagingBackend::S3);
    EXPECT_EQ(result->locator, "staging/upload-123/assembled/7.bin");
    EXPECT_EQ(result->size_bytes, expected_data.size());
    EXPECT_EQ(result->md5_hash, disk::utils::FileHashUtil::HashMd5(expected_data));
    EXPECT_EQ(result->sha256_hash, disk::utils::FileHashUtil::HashSha256(expected_data));
    EXPECT_EQ(client->objects.at(result->locator), expected_data);
    EXPECT_EQ(client->create_multipart_calls, 1);
    EXPECT_EQ(client->complete_multipart_calls, 1);
    EXPECT_EQ(client->abort_multipart_calls, 0);
    EXPECT_EQ(client->uploaded_part_sizes, std::vector<size_t>{ expected_data.size() });
    EXPECT_EQ(client->completed_part_numbers, std::vector<int>{ 1 });
}

TEST_F(S3ObjectStorageTest, S3AssemblyUsesFiveMiBNonFinalParts) {
    const auto session = S3Session("upload-large");
    constexpr size_t part_size = 5 * 1024 * 1024;
    const std::string data(part_size + 17, 'x');
    const auto chunk = WriteS3Chunk(session, 0, data);

    auto result = drogon::sync_wait(storage->AssembleChunks(
        session,
        11,
        1,
        std::vector{ chunk }
    ));

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->size_bytes, data.size());
    EXPECT_EQ(client->uploaded_part_sizes, (std::vector<size_t>{ part_size, 17 }));
    EXPECT_EQ(client->completed_part_numbers, (std::vector<int>{ 1, 2 }));
    EXPECT_EQ(client->objects.at(result->locator), data);
}

TEST_F(S3ObjectStorageTest, S3AssemblyRejectsDescriptorBeforeCreatingMultipart) {
    const auto session = S3Session();
    auto chunk = WriteS3Chunk(session, 0, "payload");
    chunk.object_key = "staging/another-upload/chunks/0-invalid.part";

    auto result = drogon::sync_wait(storage->AssembleChunks(
        session,
        2,
        1,
        std::vector{ chunk }
    ));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::ChunkVerifyFailed);
    EXPECT_EQ(client->create_multipart_calls, 0);
}

TEST_F(S3ObjectStorageTest, S3AssemblyAbortsForMissingOrMismatchedHeadMetadata) {
    const auto session = S3Session();
    const std::string data = "head-metadata";
    const auto chunk = WriteS3Chunk(session, 0, data);

    auto wrong_etag = chunk;
    wrong_etag.etag = "\"wrong-etag\"";
    auto etag_result = drogon::sync_wait(storage->AssembleChunks(
        session,
        20,
        1,
        std::vector{ wrong_etag }
    ));
    ASSERT_FALSE(etag_result.has_value());
    EXPECT_EQ(etag_result.error().code, ErrorCode::ChunkVerifyFailed);

    auto wrong_size = chunk;
    ++wrong_size.size_bytes;
    auto size_result = drogon::sync_wait(storage->AssembleChunks(
        session,
        21,
        1,
        std::vector{ wrong_size }
    ));
    ASSERT_FALSE(size_result.has_value());
    EXPECT_EQ(size_result.error().code, ErrorCode::ChunkVerifyFailed);

    client->objects.erase(chunk.object_key);
    auto missing_result = drogon::sync_wait(storage->AssembleChunks(
        session,
        22,
        1,
        std::vector{ chunk }
    ));
    ASSERT_FALSE(missing_result.has_value());
    EXPECT_EQ(missing_result.error().code, ErrorCode::ChunkVerifyFailed);

    EXPECT_EQ(client->create_multipart_calls, 3);
    EXPECT_EQ(client->abort_multipart_calls, 3);
    EXPECT_TRUE(client->multipart_uploads.empty());
}

TEST_F(S3ObjectStorageTest, S3AssemblyAbortsWhenChunkContentDoesNotMatchDescriptor) {
    const auto session = S3Session();
    const auto chunk = WriteS3Chunk(session, 0, "expected");
    client->objects[chunk.object_key] = "tampered";

    auto result = drogon::sync_wait(storage->AssembleChunks(
        session,
        3,
        1,
        std::vector{ chunk }
    ));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::ChunkVerifyFailed);
    EXPECT_EQ(client->abort_multipart_calls, 1);
    EXPECT_TRUE(client->multipart_uploads.empty());
    EXPECT_FALSE(client->objects.contains("staging/upload-123/assembled/3.bin"));
}

TEST_F(S3ObjectStorageTest, S3AssemblyAbortsWhenRangeReadIsShort) {
    const auto session = S3Session();
    const auto chunk = WriteS3Chunk(session, 0, "range-payload");
    client->truncate_next_range_by = 1;

    auto result = drogon::sync_wait(storage->AssembleChunks(
        session,
        4,
        1,
        std::vector{ chunk }
    ));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::ChunkVerifyFailed);
    EXPECT_EQ(client->abort_multipart_calls, 1);
    EXPECT_TRUE(client->multipart_uploads.empty());
}

TEST_F(S3ObjectStorageTest, S3AssemblyAbortsWhenPartUploadFails) {
    const auto session = S3Session();
    const auto chunk = WriteS3Chunk(session, 0, "part-failure");
    client->upload_part_failure_number = 1;

    auto result = drogon::sync_wait(storage->AssembleChunks(
        session,
        5,
        1,
        std::vector{ chunk }
    ));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InternalError);
    EXPECT_EQ(client->abort_multipart_calls, 1);
    EXPECT_TRUE(client->multipart_uploads.empty());
}

TEST_F(S3ObjectStorageTest, S3AssemblyAbortsWhenMultipartCompletionFails) {
    const auto session = S3Session();
    const auto chunk = WriteS3Chunk(session, 0, "complete-failure");
    client->complete_multipart_failure = true;

    auto result = drogon::sync_wait(storage->AssembleChunks(
        session,
        6,
        1,
        std::vector{ chunk }
    ));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InternalError);
    EXPECT_EQ(client->complete_multipart_calls, 1);
    EXPECT_EQ(client->abort_multipart_calls, 1);
    EXPECT_TRUE(client->multipart_uploads.empty());
}

TEST_F(S3ObjectStorageTest, S3AssemblyDiscardIsExactAndIdempotent) {
    const auto session = S3Session();
    const auto key = "staging/upload-123/assembled/9.bin";
    client->objects[key] = "assembled";
    const disk::storage::UploadStagingAssembly assembly{
        .backend = disk::storage::UploadStagingBackend::S3,
        .locator = key,
    };

    auto first = drogon::sync_wait(storage->DiscardAssembly(session, assembly));
    auto repeated = drogon::sync_wait(storage->DiscardAssembly(session, assembly));

    ASSERT_TRUE(first.has_value()) << first.error().message;
    ASSERT_TRUE(repeated.has_value()) << repeated.error().message;
    EXPECT_EQ(client->delete_calls, 2);
    EXPECT_FALSE(client->objects.contains(key));

    const disk::storage::UploadStagingAssembly outside{
        .backend = disk::storage::UploadStagingBackend::S3,
        .locator = "staging/another-upload/assembled/9.bin",
    };
    auto outside_result = drogon::sync_wait(storage->DiscardAssembly(session, outside));
    ASSERT_FALSE(outside_result.has_value());
    EXPECT_EQ(outside_result.error().code, ErrorCode::ValidationFailed);
    EXPECT_EQ(client->delete_calls, 2);
}

TEST_F(S3ObjectStorageTest, S3SessionCleanupPagesWithinExactPrefix) {
    const auto session = S3Session("upload-cleanup");
    for (size_t index = 0; index < 1005; ++index) {
        client->objects[session.prefix + "/chunks/" + std::to_string(index)] = "x";
    }
    client->objects["staging/upload-cleanup-sibling/chunks/0"] = "keep";
    client->objects["objects/ab/final.bin"] = "keep";

    auto result = drogon::sync_wait(storage->CleanupSession(session));

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(client->list_calls, 2);
    EXPECT_EQ(client->delete_batch_calls, 2);
    EXPECT_EQ(client->delete_batch_sizes, (std::vector<size_t>{ 1000, 5 }));
    EXPECT_TRUE(client->objects.contains("staging/upload-cleanup-sibling/chunks/0"));
    EXPECT_TRUE(client->objects.contains("objects/ab/final.bin"));
    EXPECT_EQ(
        std::ranges::count_if(client->objects, [&session](const auto& entry) {
            return entry.first.starts_with(session.prefix + "/");
        }),
        0
    );
}

TEST_F(S3ObjectStorageTest, S3SessionCleanupRejectsOutOfPrefixListResult) {
    const auto session = S3Session("upload-cleanup");
    client->objects[session.prefix + "/chunks/0"] = "delete-only-if-safe";
    client->list_outside_prefix_key = "objects/ab/final.bin";

    auto result = drogon::sync_wait(storage->CleanupSession(session));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InternalError);
    EXPECT_EQ(client->delete_batch_calls, 0);
    EXPECT_TRUE(client->objects.contains(session.prefix + "/chunks/0"));
}

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
