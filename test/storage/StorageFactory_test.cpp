#include "storage/StorageFactory.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <drogon/drogon.h>
#include <gtest/gtest.h>
#include <json/json.h>

#include "storage/LocalBlobStore.hpp"
#include "storage/LocalFileStorage.hpp"
#include "storage/S3ObjectStorage.hpp"
#include "utils/ConfigMgr.hpp"

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

    auto SourceSection(
        const std::string& source,
        std::string_view begin_marker,
        std::string_view end_marker
    ) -> std::string {
        const auto begin = source.find(begin_marker);
        const auto end = source.find(end_marker, begin);
        if (begin == std::string::npos || end == std::string::npos || end <= begin) {
            return {};
        }
        return source.substr(begin, end - begin);
    }

    class FakeS3Client final : public disk::storage::IS3Client {
    public:
        auto ValidateBucketAccessible(disk::utils::LogContext /*log_context*/)
            -> Result<void> override {
            ++validate_calls;
            return validate_result;
        }

        auto HeadObject(
            const std::string& /*key*/,
            disk::utils::LogContext /*log_context*/
        )
            -> Result<disk::storage::S3HeadObjectResult> override {
            return disk::storage::S3HeadObjectResult{};
        }

        auto PutObjectIfAbsent(
            const std::string& /*key*/,
            std::string /*data*/,
            disk::utils::LogContext /*log_context*/
        )
            -> Result<disk::storage::S3PutObjectResult> override {
            return disk::storage::S3PutObjectResult{ .created = true };
        }

        auto PutObjectFromFileIfAbsent(
            const std::string& /*key*/,
            const std::filesystem::path& /*local_path*/,
            disk::utils::LogContext /*log_context*/
        ) -> Result<disk::storage::S3PutObjectResult> override {
            return disk::storage::S3PutObjectResult{ .created = true };
        }

        auto DeleteObject(
            const std::string& /*key*/,
            disk::utils::LogContext /*log_context*/
        ) -> Result<void> override {
            return {};
        }

        auto GetObjectRange(
            const std::string& /*key*/,
            uint64_t /*start*/,
            uint64_t /*length*/,
            disk::utils::LogContext /*log_context*/
        ) -> Result<std::shared_ptr<disk::storage::StorageReadStream>> override {
            return std::unexpected(ErrorInfo(ErrorCode::FileNotFound, "unused fake range"));
        }

        auto ListObjects(
            const std::string& /*prefix*/,
            const std::string& /*continuation_token*/,
            uint32_t /*max_keys*/,
            disk::utils::LogContext /*log_context*/
        ) -> Result<disk::storage::S3ListObjectsResult> override {
            return disk::storage::S3ListObjectsResult{};
        }

        auto DeleteObjects(
            const std::vector<std::string>& /*keys*/,
            disk::utils::LogContext /*log_context*/
        ) -> Result<void> override {
            return {};
        }

        auto CreateMultipartUpload(
            const std::string& /*key*/,
            disk::utils::LogContext /*log_context*/
        ) -> Result<std::string> override {
            return "unused-upload";
        }

        auto UploadPart(
            const std::string& /*key*/,
            const std::string& /*upload_id*/,
            int /*part_number*/,
            std::string /*data*/,
            disk::utils::LogContext /*log_context*/
        ) -> Result<std::string> override {
            return "unused-etag";
        }

        auto UploadPartCopy(
            const std::string& /*source_key*/,
            const std::string& /*destination_key*/,
            const std::string& /*upload_id*/,
            int /*part_number*/,
            uint64_t /*start*/,
            uint64_t /*length*/,
            disk::utils::LogContext /*log_context*/
        ) -> Result<std::string> override {
            return "unused-etag";
        }

        auto CompleteMultipartUpload(
            const std::string& /*key*/,
            const std::string& /*upload_id*/,
            const std::vector<disk::storage::S3CompletedPart>& /*parts*/,
            bool /*only_if_absent*/,
            disk::utils::LogContext /*log_context*/
        ) -> Result<disk::storage::S3CompleteMultipartResult> override {
            return disk::storage::S3CompleteMultipartResult{ .created = true };
        }

        auto AbortMultipartUpload(
            const std::string& /*key*/,
            const std::string& /*upload_id*/,
            disk::utils::LogContext /*log_context*/
        ) -> Result<void> override {
            return {};
        }

        Result<void> validate_result{};
        int validate_calls{ 0 };
    };

    auto LoadStorageConfig(
        const std::string& backend,
        const std::filesystem::path& root
    ) -> std::shared_ptr<disk::utils::ConfigMgr> {
        Json::Value cfg;
        auto& disk = cfg["custom_config"]["disk"];
        disk["storage_backend"] = backend;
        disk["storage_base_path"] = (root / "blobs").string();
        disk["temp_upload_path"] = (root / "temp").string();
        disk["assembly_max_concurrent"] = 2;
        disk["assemble_buffer_size_bytes"] = 4096;
        disk["s3"]["bucket"] = "factory-test";
        disk["s3"]["region"] = "us-east-1";
        disk["s3"]["object_prefix"] = "objects";
        drogon::app().loadConfigJson(cfg);

        auto config_mgr = disk::utils::ConfigMgr::GetInstance();
        config_mgr->LoadConfig();
        return config_mgr;
    }

    class StorageFactoryTest : public ::testing::Test {
    protected:
        void SetUp() override {
            const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
            root = std::filesystem::path("build/test_storage_factory") /
                   (std::string(info->test_suite_name()) + "_" + info->name());
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
            std::filesystem::create_directories(root, ec);
        }

        void TearDown() override {
            LoadStorageConfig("local", root);
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }

        std::filesystem::path root;
    };

} // namespace

TEST(StorageRuntimeLogContextContractTest, UsesTypedContextAndBoundedDeploymentDetails) {
    const auto context_source = ReadSourceFile("src/storage/StorageLogContext.hpp");
    const auto factory_source = ReadSourceFile("src/storage/StorageFactory.cpp");
    const auto local_file_source = ReadSourceFile("src/storage/LocalFileStorage.cpp");
    const auto local_blob_source = ReadSourceFile("src/storage/LocalBlobStore.cpp");
    const auto s3_source = ReadSourceFile("src/storage/S3ObjectStorage.cpp");
    const auto combined_source = factory_source + local_file_source + local_blob_source + s3_source;
    const auto s3_constructor = SourceSection(
        s3_source,
        "S3ObjectStorage::S3ObjectStorage(",
        "auto S3ObjectStorage::SetMultipartUploadJournal("
    );

    EXPECT_EQ(CountOccurrences(context_source, ".operation = \"storage_runtime\""), 1U);
    EXPECT_FALSE(Contains(context_source, ".request_id ="));
    EXPECT_FALSE(Contains(context_source, ".upload_id ="));
    EXPECT_FALSE(Contains(context_source, ".job_id ="));
    EXPECT_FALSE(Contains(context_source, ".lease_owner ="));
    EXPECT_FALSE(Contains(context_source, ".state_version ="));
    EXPECT_EQ(CountOccurrences(combined_source, "Logger::Info(StorageRuntimeLogContext())"), 5U);
    EXPECT_FALSE(Contains(combined_source, "Logger::Info()"));
    EXPECT_TRUE(Contains(factory_source, "Storage backend selected: backend=local"));
    EXPECT_TRUE(Contains(factory_source, "Storage backend selected: backend=s3"));
    EXPECT_TRUE(Contains(local_file_source, "Local file storage initialized: io_threads="));
    EXPECT_TRUE(Contains(local_blob_source, "Local blob storage initialized: io_threads="));
    EXPECT_TRUE(Contains(s3_constructor, "S3 object storage initialized: max_connections="));
    EXPECT_FALSE(s3_constructor.empty());
    EXPECT_FALSE(Contains(s3_constructor, "bucket="));
    EXPECT_FALSE(Contains(s3_constructor, "prefix="));
    EXPECT_FALSE(Contains(s3_constructor, "endpoint="));
    EXPECT_FALSE(Contains(s3_constructor, "region="));
    EXPECT_FALSE(Contains(s3_constructor, "instance_id="));
    EXPECT_FALSE(Contains(s3_constructor, "e.what()"));
}

TEST(StorageCapabilityBoundaryContractTest, UsesOnlyExplicitStorageCapabilities) {
    const auto factory_header = ReadSourceFile("src/storage/StorageFactory.hpp");
    const auto manager_header = ReadSourceFile("src/storage/StorageMgr.hpp");
    const auto blob_manager_header = ReadSourceFile("src/storage/BlobStoreMgr.hpp");
    const auto staging_store_header = ReadSourceFile("src/storage/UploadStagingStorage.hpp");
    const auto blob_store_header = ReadSourceFile("src/storage/IBlobStore.hpp");
    const auto application_header = ReadSourceFile("src/application/ApplicationContext.hpp");
    const auto upload_header = ReadSourceFile("src/services/UploadService.hpp");
    const auto lifecycle_header = ReadSourceFile("src/services/UploadLifecycleService.hpp");
    const auto local_header = ReadSourceFile("src/storage/LocalFileStorage.hpp");
    const auto local_blob_header = ReadSourceFile("src/storage/LocalBlobStore.hpp");
    const auto local_source = ReadSourceFile("src/storage/LocalFileStorage.cpp");
    const auto s3_header = ReadSourceFile("src/storage/S3ObjectStorage.hpp");
    const auto staging_head_capability = SourceSection(
        staging_store_header,
        "virtual auto HeadChunkObject(",
        "virtual auto AssembleChunks("
    );
    const auto staging_inventory_capability = SourceSection(
        staging_store_header,
        "virtual auto ListStagingObjects(",
        "    };"
    );
    const auto final_inventory_capability = SourceSection(
        blob_store_header,
        "virtual auto ListFinalObjects(",
        "    };"
    );

    EXPECT_FALSE(std::filesystem::exists(RepositoryRoot() / "src/storage/IFileStorage.hpp"));
    EXPECT_TRUE(Contains(factory_header, "std::shared_ptr<UploadStagingStorage> upload_staging_storage;"));
    EXPECT_TRUE(Contains(manager_header, "SetInstance(std::shared_ptr<UploadStagingStorage> storage)"));
    EXPECT_FALSE(Contains(manager_header, "GetStorage()"));
    EXPECT_FALSE(Contains(manager_header, "IsInitialized()"));
    EXPECT_FALSE(Contains(blob_manager_header, "IsInitialized()"));
    EXPECT_FALSE(staging_head_capability.empty());
    EXPECT_TRUE(Contains(staging_head_capability, "= 0;"));
    EXPECT_FALSE(staging_inventory_capability.empty());
    EXPECT_TRUE(Contains(staging_inventory_capability, "= 0;"));
    EXPECT_FALSE(final_inventory_capability.empty());
    EXPECT_TRUE(Contains(final_inventory_capability, "= 0;"));
    EXPECT_FALSE(Contains(staging_store_header, "Staging object HEAD is not supported"));
    EXPECT_FALSE(Contains(staging_store_header, "Staging inventory is not supported"));
    EXPECT_FALSE(Contains(blob_store_header, "Final Blob inventory is not supported"));
    EXPECT_TRUE(Contains(blob_store_header, "virtual auto BlobExists("));
    EXPECT_FALSE(Contains(blob_store_header, "virtual auto Exists("));
    EXPECT_TRUE(Contains(blob_store_header, "virtual auto OpenBlobRangeForRead("));
    EXPECT_FALSE(Contains(blob_store_header, "virtual auto OpenForRead("));
    EXPECT_FALSE(Contains(blob_store_header, "virtual auto OpenBlobForRead("));
    EXPECT_FALSE(Contains(blob_store_header, "class FileStorageReadStream"));
    EXPECT_FALSE(Contains(blob_store_header, "GetFinalStoragePath("));
    EXPECT_FALSE(Contains(local_blob_header, "GetFinalStoragePath("));
    EXPECT_FALSE(Contains(s3_header, "GetFinalStoragePath("));
    EXPECT_TRUE(Contains(blob_store_header, "virtual auto GetBlobSize("));
    EXPECT_FALSE(Contains(blob_store_header, "GetFileSize("));
    EXPECT_FALSE(Contains(local_blob_header, "GetFileSize("));
    EXPECT_FALSE(Contains(s3_header, "GetFileSize("));
    EXPECT_FALSE(Contains(application_header, "IFileStorage"));
    EXPECT_FALSE(Contains(upload_header, "IFileStorage"));
    EXPECT_FALSE(Contains(lifecycle_header, "IFileStorage"));
    EXPECT_TRUE(Contains(local_header, "class LocalFileStorage : public UploadStagingStorage"));
    EXPECT_TRUE(Contains(local_header, "auto HeadChunkObject("));
    EXPECT_TRUE(Contains(local_header, "auto ListStagingObjects("));
    EXPECT_FALSE(Contains(local_header, "DeletePath("));
    EXPECT_FALSE(Contains(local_source, "RunBlockingFilesystemTaskWithTimeout"));
    EXPECT_TRUE(Contains(local_header, "DiscardAssembly("));
    EXPECT_TRUE(Contains(local_blob_header, "auto ListFinalObjects("));
    EXPECT_TRUE(Contains(s3_header, "class S3ObjectStorage final : public IBlobStore,"));
    EXPECT_TRUE(Contains(s3_header, "auto HeadChunkObject("));
    EXPECT_TRUE(Contains(s3_header, "auto ListStagingObjects("));
    EXPECT_TRUE(Contains(s3_header, "auto ListFinalObjects("));
}

TEST_F(StorageFactoryTest, SelectsLocalStorageWithoutCreatingS3Client) {
    auto config_mgr = LoadStorageConfig("local", root);
    bool factory_called = false;

    auto bundle = disk::storage::StorageFactory::Create(
        config_mgr,
        [&](const disk::utils::S3StorageConfig&) -> std::shared_ptr<disk::storage::IS3Client> {
            factory_called = true;
            return std::make_shared<FakeS3Client>();
        }
    );

    EXPECT_FALSE(factory_called);
    EXPECT_NE(
        std::dynamic_pointer_cast<disk::storage::LocalFileStorage>(bundle.upload_staging_storage),
        nullptr
    );
    EXPECT_NE(std::dynamic_pointer_cast<disk::storage::LocalBlobStore>(bundle.blob_store), nullptr);
}

TEST_F(StorageFactoryTest, SelectsOneSharedS3StorageAfterBucketValidation) {
    auto config_mgr = LoadStorageConfig("s3", root);
    auto client = std::make_shared<FakeS3Client>();
    std::string created_bucket;

    auto bundle = disk::storage::StorageFactory::Create(
        config_mgr,
        [&](const disk::utils::S3StorageConfig& config)
            -> std::shared_ptr<disk::storage::IS3Client> {
            created_bucket = config.bucket;
            return client;
        }
    );

    auto storage_backend =
        std::dynamic_pointer_cast<disk::storage::S3ObjectStorage>(bundle.upload_staging_storage);
    auto blob_backend = std::dynamic_pointer_cast<disk::storage::S3ObjectStorage>(bundle.blob_store);
    ASSERT_NE(storage_backend, nullptr);
    ASSERT_NE(blob_backend, nullptr);
    EXPECT_EQ(storage_backend, blob_backend);
    EXPECT_EQ(created_bucket, "factory-test");
    EXPECT_EQ(client->validate_calls, 1);
}

TEST_F(StorageFactoryTest, MapsBucketValidationFailureToStableInitializationError) {
    auto config_mgr = LoadStorageConfig("s3", root);
    auto client = std::make_shared<FakeS3Client>();
    client->validate_result = std::unexpected(
        ErrorInfo(ErrorCode::InternalError, "S3 GetBucketLocation failed: access denied")
    );

    try {
        (void)disk::storage::StorageFactory::Create(
            config_mgr,
            [client](const disk::utils::S3StorageConfig&)
                -> std::shared_ptr<disk::storage::IS3Client> { return client; }
        );
        FAIL() << "Expected S3 initialization to fail";
    } catch (const std::runtime_error& e) {
        EXPECT_EQ(
            std::string(e.what()),
            "Failed to initialize S3 storage backend: S3 GetBucketLocation failed: access denied"
        );
    }

    EXPECT_EQ(client->validate_calls, 1);
}
