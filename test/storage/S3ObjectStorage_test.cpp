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

        auto PutObjectFromFileIfAbsent(
            const std::string& key,
            const std::filesystem::path& local_path
        ) -> Result<disk::storage::S3PutObjectResult> override {
            ++put_calls;
            uploaded_keys.push_back(key);
            if (objects.contains(key)) {
                return disk::storage::S3PutObjectResult{ .created = false };
            }
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
            return disk::storage::S3PutObjectResult{
                .etag = EtagFor(objects.at(key)),
                .created = true,
            };
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
            ++upload_part_calls;
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
            copied_ranges.push_back(CopiedRange{
                .source_key = source_key,
                .destination_key = destination_key,
                .part_number = part_number,
                .start = start,
                .length = length,
            });
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
            const std::vector<disk::storage::S3CompletedPart>& parts,
            bool only_if_absent
        ) -> Result<disk::storage::S3CompleteMultipartResult> override {
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
            if (concurrent_final_content.has_value() && only_if_absent) {
                objects[key] = concurrent_final_content.value();
                concurrent_final_content.reset();
            }
            if (only_if_absent && objects.contains(key)) {
                return disk::storage::S3CompleteMultipartResult{ .created = false };
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
            return disk::storage::S3CompleteMultipartResult{ .created = true };
        }

        auto AbortMultipartUpload(const std::string& /*key*/, const std::string& upload_id)
            -> Result<void> override {
            ++abort_multipart_calls;
            aborted_upload_ids.push_back(upload_id);
            if (abort_multipart_failure) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "fake multipart abort failure")
                );
            }
            multipart_uploads.erase(upload_id);
            return {};
        }

        struct MultipartUpload {
            std::string key;
            std::map<int, std::string> parts;
        };

        struct CopiedRange {
            std::string source_key;
            std::string destination_key;
            int part_number{ 0 };
            uint64_t start{ 0 };
            uint64_t length{ 0 };

            auto operator==(const CopiedRange&) const -> bool = default;
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
        int upload_part_calls{ 0 };
        int upload_part_failure_number{ 0 };
        bool complete_multipart_failure{ false };
        bool abort_multipart_failure{ false };
        std::optional<std::string> concurrent_final_content;
        std::optional<std::string> list_outside_prefix_key;
        std::vector<size_t> delete_batch_sizes;
        std::vector<size_t> uploaded_part_sizes;
        std::vector<int> completed_part_numbers;
        std::vector<std::string> aborted_upload_ids;
        std::vector<CopiedRange> copied_ranges;
        std::map<std::string, MultipartUpload> multipart_uploads;
        ErrorInfo delete_error{ ErrorCode::InternalError, "fake delete failure" };
    };

    class RecordingMultipartUploadJournal final
        : public disk::storage::IMultipartUploadJournal {
    public:
        auto Track(const disk::storage::MultipartUploadDescriptor& descriptor)
            -> Result<void> override {
            tracked.push_back(descriptor);
            if (track_error.has_value()) {
                return std::unexpected(track_error.value());
            }
            return {};
        }

        auto Renew(const disk::storage::MultipartUploadDescriptor& descriptor)
            -> Result<void> override {
            renewed.push_back(descriptor);
            if (renew_error.has_value()) {
                return std::unexpected(renew_error.value());
            }
            return {};
        }

        auto Resolve(const disk::storage::MultipartUploadDescriptor& descriptor)
            -> Result<void> override {
            resolved.push_back(descriptor);
            return {};
        }

        auto ReleaseForRetry(
            const disk::storage::MultipartUploadDescriptor& descriptor,
            const std::string& error
        ) -> Result<void> override {
            released_for_retry.push_back(descriptor);
            retry_errors.push_back(error);
            return {};
        }

        std::optional<ErrorInfo> track_error;
        std::optional<ErrorInfo> renew_error;
        std::vector<disk::storage::MultipartUploadDescriptor> tracked;
        std::vector<disk::storage::MultipartUploadDescriptor> renewed;
        std::vector<disk::storage::MultipartUploadDescriptor> resolved;
        std::vector<disk::storage::MultipartUploadDescriptor> released_for_retry;
        std::vector<std::string> retry_errors;
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
            journal = std::make_shared<RecordingMultipartUploadJournal>();
            storage->SetMultipartUploadJournal(journal);
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
        std::shared_ptr<RecordingMultipartUploadJournal> journal;
        std::unique_ptr<disk::storage::S3ObjectStorage> storage;
    };

    auto LocalAssembly(
        const std::filesystem::path& path,
        const std::string& content
    ) -> disk::storage::UploadStagingAssembly {
        return disk::storage::UploadStagingAssembly{
            .backend = disk::storage::UploadStagingBackend::Local,
            .locator = path.string(),
            .size_bytes = content.size(),
            .md5_hash = disk::utils::FileHashUtil::HashMd5(content),
            .sha256_hash = disk::utils::FileHashUtil::HashSha256(content),
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

TEST_F(S3ObjectStorageTest, InventoriesUseConfiguredPrefixesAndContinuationTokens) {
    client->objects["staging/upload-a/chunks/0-a.part"] = "a";
    client->objects["staging/upload-b/chunks/0-b.part"] = "bb";
    client->objects["staging/upload-c/assembled/1.bin"] = "ccc";
    client->objects["objects/sha256/aa/a.bin"] = "final";
    client->objects["outside/ignored.bin"] = "ignored";

    auto first = drogon::sync_wait(storage->ListStagingObjects({}, 2));
    ASSERT_TRUE(first.has_value()) << first.error().message;
    ASSERT_EQ(first->objects.size(), 2U);
    EXPECT_EQ(first->objects[0].locator, "staging/upload-a/chunks/0-a.part");
    EXPECT_EQ(first->objects[0].size_bytes, 1U);
    EXPECT_EQ(first->objects[1].locator, "staging/upload-b/chunks/0-b.part");
    EXPECT_EQ(first->objects[1].size_bytes, 2U);
    EXPECT_TRUE(first->has_more);
    EXPECT_FALSE(first->continuation_token.empty());

    auto second = drogon::sync_wait(
        storage->ListStagingObjects(first->continuation_token, 2)
    );
    ASSERT_TRUE(second.has_value()) << second.error().message;
    ASSERT_EQ(second->objects.size(), 1U);
    EXPECT_EQ(second->objects[0].locator, "staging/upload-c/assembled/1.bin");
    EXPECT_FALSE(second->has_more);

    auto final = drogon::sync_wait(storage->ListFinalObjects({}, 10));
    ASSERT_TRUE(final.has_value()) << final.error().message;
    ASSERT_EQ(final->objects.size(), 1U);
    EXPECT_EQ(final->objects[0].locator, "objects/sha256/aa/a.bin");
    EXPECT_EQ(final->objects[0].size_bytes, 5U);
}

TEST_F(S3ObjectStorageTest, InventoryRejectsInvalidPageAndOutOfPrefixResult) {
    auto invalid = drogon::sync_wait(storage->ListFinalObjects({}, 0));
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, ErrorCode::ValidationFailed);
    EXPECT_EQ(client->list_calls, 0);

    client->list_outside_prefix_key = "staging/unsafe.bin";
    auto outside = drogon::sync_wait(storage->ListFinalObjects({}, 10));
    ASSERT_FALSE(outside.has_value());
    EXPECT_EQ(outside.error().code, ErrorCode::InternalError);
    EXPECT_EQ(client->head_calls, 0);
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
    ASSERT_EQ(journal->tracked.size(), 1U);
    EXPECT_EQ(journal->tracked[0].key, result->locator);
    EXPECT_EQ(journal->renewed.size(), 2U);
    EXPECT_EQ(journal->resolved.size(), 1U);
    EXPECT_TRUE(journal->released_for_retry.empty());
}

TEST_F(S3ObjectStorageTest, MultipartJournalFailurePreventsPartUploadAndAborts) {
    const auto session = S3Session();
    const auto chunk = WriteS3Chunk(session, 0, "journal-failure");
    journal->track_error = ErrorInfo(ErrorCode::InternalError, "database unavailable");

    auto result = drogon::sync_wait(storage->AssembleChunks(
        session,
        30,
        1,
        std::vector{ chunk }
    ));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InternalError);
    EXPECT_EQ(client->upload_part_calls, 0);
    EXPECT_EQ(client->abort_multipart_calls, 1);
    EXPECT_TRUE(client->multipart_uploads.empty());
    EXPECT_EQ(journal->tracked.size(), 1U);
    EXPECT_TRUE(journal->resolved.empty());
}

TEST_F(S3ObjectStorageTest, MultipartCreationRequiresRecoveryJournal) {
    const auto session = S3Session();
    const auto chunk = WriteS3Chunk(session, 0, "journal-required");
    disk::storage::S3ObjectStorage storage_without_journal(
        disk::utils::ConfigMgr::GetInstance(),
        client
    );

    auto result = drogon::sync_wait(storage_without_journal.AssembleChunks(
        session,
        33,
        1,
        std::vector{ chunk }
    ));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InternalError);
    EXPECT_EQ(client->create_multipart_calls, 0);
    EXPECT_TRUE(client->multipart_uploads.empty());
}

TEST_F(S3ObjectStorageTest, MultipartAbortFailureReleasesTrackedTaskForRetry) {
    const auto session = S3Session();
    const auto chunk = WriteS3Chunk(session, 0, "abort-failure");
    client->upload_part_failure_number = 1;
    client->abort_multipart_failure = true;

    auto result = drogon::sync_wait(storage->AssembleChunks(
        session,
        31,
        1,
        std::vector{ chunk }
    ));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(client->abort_multipart_calls, 1);
    EXPECT_EQ(journal->tracked.size(), 1U);
    EXPECT_TRUE(journal->resolved.empty());
    ASSERT_EQ(journal->released_for_retry.size(), 1U);
    EXPECT_EQ(journal->released_for_retry[0], journal->tracked[0]);
    ASSERT_EQ(journal->retry_errors.size(), 1U);
    EXPECT_EQ(journal->retry_errors[0], "fake multipart abort failure");
    EXPECT_EQ(client->multipart_uploads.size(), 1U);
}

TEST_F(S3ObjectStorageTest, MultipartLeaseLossStopsBeforePartAndAborts) {
    const auto session = S3Session();
    const auto chunk = WriteS3Chunk(session, 0, "lease-loss");
    journal->renew_error = ErrorInfo(ErrorCode::ResourceConflict, "lease lost");

    auto result = drogon::sync_wait(storage->AssembleChunks(
        session,
        32,
        1,
        std::vector{ chunk }
    ));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::ResourceConflict);
    EXPECT_EQ(client->upload_part_calls, 0);
    EXPECT_EQ(client->abort_multipart_calls, 1);
    EXPECT_EQ(journal->resolved.size(), 1U);
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

TEST_F(S3ObjectStorageTest, GetFinalStoragePathUsesSha256Namespace) {
    const std::string sha256 = "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
    EXPECT_EQ(
        storage->GetFinalStoragePath(sha256).generic_string(),
        "objects/sha256/ab/" + sha256 + ".bin"
    );
}

TEST_F(S3ObjectStorageTest, LocalAssemblyPromotionConditionallyCreatesSha256Object) {
    const std::string content = "hello s3";
    const auto sha256 = disk::utils::FileHashUtil::HashSha256(content);
    const auto temp_path = WriteTempFile("assembled.tmp", content);
    const auto final_key = "objects/sha256/" + sha256.substr(0, 2) + "/" + sha256 + ".bin";

    auto result = drogon::sync_wait(
        storage->PromoteToFinal(LocalAssembly(temp_path, content), sha256)
    );

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_TRUE(result->created);
    EXPECT_EQ(result->path.generic_string(), final_key);
    EXPECT_EQ(client->put_calls, 1);
    EXPECT_EQ(client->objects[final_key], content);
    EXPECT_TRUE(std::filesystem::exists(temp_path));
}

TEST_F(S3ObjectStorageTest, LocalAssemblyPromotionReusesSameSizeObject) {
    const std::string content = "existing";
    const auto sha256 = disk::utils::FileHashUtil::HashSha256(content);
    const auto final_key = "objects/sha256/" + sha256.substr(0, 2) + "/" + sha256 + ".bin";
    client->objects[final_key] = content;
    const auto temp_path = WriteTempFile("assembled.tmp", content);

    auto result = drogon::sync_wait(
        storage->PromoteToFinal(LocalAssembly(temp_path, content), sha256)
    );

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FALSE(result->created);
    EXPECT_EQ(client->put_calls, 0);
    EXPECT_EQ(client->objects[final_key], content);
    EXPECT_TRUE(std::filesystem::exists(temp_path));
}

TEST_F(S3ObjectStorageTest, PromotionRejectsInvalidSha256BeforeStorageAccess) {
    const std::string content = "invalid hash";
    const auto temp_path = WriteTempFile("assembled.tmp", content);

    auto result = drogon::sync_wait(
        storage->PromoteToFinal(LocalAssembly(temp_path, content), "abcdef")
    );

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    EXPECT_EQ(client->head_calls, 0);
}

TEST_F(S3ObjectStorageTest, S3AssemblyPromotionUsesMultipartServerSideCopy) {
    constexpr size_t part_size = 5 * 1024 * 1024;
    const std::string content(part_size + 17, 'p');
    const auto sha256 = disk::utils::FileHashUtil::HashSha256(content);
    const auto source_key = "staging/upload-promote/assembled/9.bin";
    const auto final_key = "objects/sha256/" + sha256.substr(0, 2) + "/" + sha256 + ".bin";
    client->objects[source_key] = content;
    const disk::storage::UploadStagingAssembly assembly{
        .backend = disk::storage::UploadStagingBackend::S3,
        .locator = source_key,
        .size_bytes = content.size(),
        .md5_hash = disk::utils::FileHashUtil::HashMd5(content),
        .sha256_hash = sha256,
    };

    auto result = drogon::sync_wait(storage->PromoteToFinal(assembly, sha256));

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_TRUE(result->created);
    EXPECT_EQ(result->path.generic_string(), final_key);
    EXPECT_EQ(client->objects.at(final_key), content);
    EXPECT_EQ(client->objects.at(source_key), content);
    ASSERT_EQ(client->copied_ranges.size(), 2U);
    EXPECT_EQ(client->copied_ranges[0], (FakeS3Client::CopiedRange{
                                            .source_key = source_key,
                                            .destination_key = final_key,
                                            .part_number = 1,
                                            .start = 0,
                                            .length = part_size,
                                        }));
    EXPECT_EQ(client->copied_ranges[1], (FakeS3Client::CopiedRange{
                                            .source_key = source_key,
                                            .destination_key = final_key,
                                            .part_number = 2,
                                            .start = part_size,
                                            .length = 17,
                                        }));
    EXPECT_EQ(client->abort_multipart_calls, 0);
    ASSERT_EQ(journal->tracked.size(), 1U);
    EXPECT_EQ(journal->tracked[0].key, final_key);
    EXPECT_EQ(journal->resolved.size(), 1U);
    EXPECT_TRUE(journal->released_for_retry.empty());
}

TEST_F(S3ObjectStorageTest, S3AssemblyPromotionAbortsWhenCopyPartFails) {
    constexpr size_t part_size = 5 * 1024 * 1024;
    const std::string content(part_size + 1, 'f');
    const auto sha256 = disk::utils::FileHashUtil::HashSha256(content);
    const auto source_key = "staging/upload-copy-fail/assembled/3.bin";
    const auto final_key = "objects/sha256/" + sha256.substr(0, 2) + "/" + sha256 + ".bin";
    client->objects[source_key] = content;
    client->upload_part_failure_number = 2;
    const disk::storage::UploadStagingAssembly assembly{
        .backend = disk::storage::UploadStagingBackend::S3,
        .locator = source_key,
        .size_bytes = content.size(),
        .sha256_hash = sha256,
    };

    auto result = drogon::sync_wait(storage->PromoteToFinal(assembly, sha256));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(client->abort_multipart_calls, 1);
    EXPECT_TRUE(client->multipart_uploads.empty());
    EXPECT_FALSE(client->objects.contains(final_key));
    EXPECT_TRUE(client->objects.contains(source_key));
}

TEST_F(S3ObjectStorageTest, S3AssemblyPromotionAbortsWhenCompletionFails) {
    const std::string content = "final-completion-failure";
    const auto sha256 = disk::utils::FileHashUtil::HashSha256(content);
    const auto source_key = "staging/upload-complete-fail/assembled/6.bin";
    const auto final_key = "objects/sha256/" + sha256.substr(0, 2) + "/" + sha256 + ".bin";
    client->objects[source_key] = content;
    client->complete_multipart_failure = true;
    const disk::storage::UploadStagingAssembly assembly{
        .backend = disk::storage::UploadStagingBackend::S3,
        .locator = source_key,
        .size_bytes = content.size(),
        .sha256_hash = sha256,
    };

    auto result = drogon::sync_wait(storage->PromoteToFinal(assembly, sha256));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(client->complete_multipart_calls, 1);
    EXPECT_EQ(client->abort_multipart_calls, 1);
    EXPECT_TRUE(client->multipart_uploads.empty());
    EXPECT_FALSE(client->objects.contains(final_key));
    EXPECT_TRUE(client->objects.contains(source_key));
}

TEST_F(S3ObjectStorageTest, PromotionRejectsAssemblyLocatorOutsideConfiguredPrefix) {
    const std::string content = "outside-staging-prefix";
    const auto sha256 = disk::utils::FileHashUtil::HashSha256(content);
    const disk::storage::UploadStagingAssembly assembly{
        .backend = disk::storage::UploadStagingBackend::S3,
        .locator = "other-staging/upload-123/assembled/1.bin",
        .size_bytes = content.size(),
        .sha256_hash = sha256,
    };

    auto result = drogon::sync_wait(storage->PromoteToFinal(assembly, sha256));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
    EXPECT_EQ(client->head_calls, 0);
    EXPECT_EQ(client->create_multipart_calls, 0);
}

TEST_F(S3ObjectStorageTest, S3AssemblyPromotionAbortsWhenConditionalCompleteLosesRace) {
    const std::string content = "concurrent final object";
    const auto sha256 = disk::utils::FileHashUtil::HashSha256(content);
    const auto source_key = "staging/upload-race/assembled/4.bin";
    const auto final_key = "objects/sha256/" + sha256.substr(0, 2) + "/" + sha256 + ".bin";
    client->objects[source_key] = content;
    client->concurrent_final_content = content;
    const disk::storage::UploadStagingAssembly assembly{
        .backend = disk::storage::UploadStagingBackend::S3,
        .locator = source_key,
        .size_bytes = content.size(),
        .sha256_hash = sha256,
    };

    auto result = drogon::sync_wait(storage->PromoteToFinal(assembly, sha256));

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FALSE(result->created);
    EXPECT_EQ(client->objects.at(final_key), content);
    EXPECT_EQ(client->abort_multipart_calls, 1);
    EXPECT_TRUE(client->multipart_uploads.empty());
}

TEST_F(S3ObjectStorageTest, PromotionRejectsExistingFinalSizeMismatchWithoutOverwrite) {
    const std::string content = "expected-content";
    const auto sha256 = disk::utils::FileHashUtil::HashSha256(content);
    const auto source_key = "staging/upload-size-conflict/assembled/5.bin";
    const auto final_key = "objects/sha256/" + sha256.substr(0, 2) + "/" + sha256 + ".bin";
    client->objects[source_key] = content;
    client->objects[final_key] = "wrong";
    const disk::storage::UploadStagingAssembly assembly{
        .backend = disk::storage::UploadStagingBackend::S3,
        .locator = source_key,
        .size_bytes = content.size(),
        .sha256_hash = sha256,
    };

    auto result = drogon::sync_wait(storage->PromoteToFinal(assembly, sha256));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::ChunkVerifyFailed);
    EXPECT_EQ(client->objects.at(final_key), "wrong");
    EXPECT_EQ(client->create_multipart_calls, 0);
}

TEST_F(S3ObjectStorageTest, ExistsSizeRangeAndDeleteUsePersistedObjectKey) {
    client->objects["objects/ab/abcdef0123456789abcdef0123456789.bin"] = "0123456789";
    const auto key = std::filesystem::path("objects/ab/abcdef0123456789abcdef0123456789.bin");

    auto exists = drogon::sync_wait(storage->Exists(key));
    ASSERT_TRUE(exists.has_value()) << exists.error().message;
    EXPECT_TRUE(*exists);

    auto size = drogon::sync_wait(storage->GetFileSize(key));
    ASSERT_TRUE(size.has_value()) << size.error().message;
    EXPECT_EQ(*size, 10U);

    auto stream_result = drogon::sync_wait(storage->OpenBlobRangeForRead(
        disk::storage::BlobDescriptor{
            .content_id = 1,
            .hash_md5 = "00000000000000000000000000000000",
            .storage_path = key.generic_string(),
            .size = 10,
        },
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

TEST_F(S3ObjectStorageTest, FinalObjectOperationsRejectKeysOutsideConfiguredPrefix) {
    const auto outside = std::filesystem::path("staging/upload-123/assembled/1.bin");
    const auto traversal = std::filesystem::path("objects/../staging/object.bin");

    auto outside_exists = drogon::sync_wait(storage->Exists(outside));
    auto traversal_delete = drogon::sync_wait(storage->DeleteBlob(traversal));
    auto range_result = drogon::sync_wait(storage->OpenBlobRangeForRead(
        disk::storage::BlobDescriptor{
            .content_id = 1,
            .hash_md5 = "00000000000000000000000000000000",
            .storage_path = outside.generic_string(),
            .size = 1,
        },
        0,
        1
    ));

    ASSERT_FALSE(outside_exists.has_value());
    ASSERT_FALSE(traversal_delete.has_value());
    ASSERT_FALSE(range_result.has_value());
    EXPECT_EQ(outside_exists.error().code, ErrorCode::ValidationFailed);
    EXPECT_EQ(traversal_delete.error().code, ErrorCode::ValidationFailed);
    EXPECT_EQ(range_result.error().code, ErrorCode::ValidationFailed);
    EXPECT_EQ(client->head_calls, 0);
    EXPECT_EQ(client->delete_calls, 0);
    EXPECT_EQ(client->get_calls, 0);
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
