#include "storage/StorageFactory.hpp"

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

#include <drogon/drogon.h>
#include <gtest/gtest.h>
#include <json/json.h>

#include "storage/LocalBlobStore.hpp"
#include "storage/LocalFileStorage.hpp"
#include "storage/S3ObjectStorage.hpp"
#include "utils/ConfigMgr.hpp"

namespace {

    class FakeS3Client final : public disk::storage::IS3Client {
    public:
        auto ValidateBucketAccessible() -> Result<void> override {
            ++validate_calls;
            return validate_result;
        }

        auto HeadObject(const std::string& /*key*/)
            -> Result<disk::storage::S3HeadObjectResult> override {
            return disk::storage::S3HeadObjectResult{};
        }

        auto PutObjectIfAbsent(const std::string& /*key*/, std::string /*data*/)
            -> Result<disk::storage::S3PutObjectResult> override {
            return disk::storage::S3PutObjectResult{ .created = true };
        }

        auto PutObjectFromFileIfAbsent(
            const std::string& /*key*/,
            const std::filesystem::path& /*local_path*/
        ) -> Result<disk::storage::S3PutObjectResult> override {
            return disk::storage::S3PutObjectResult{ .created = true };
        }

        auto DeleteObject(const std::string& /*key*/) -> Result<void> override { return {}; }

        auto GetObjectRange(
            const std::string& /*key*/,
            uint64_t /*start*/,
            uint64_t /*length*/
        ) -> Result<std::shared_ptr<disk::storage::StorageReadStream>> override {
            return std::unexpected(ErrorInfo(ErrorCode::FileNotFound, "unused fake range"));
        }

        auto ListObjects(
            const std::string& /*prefix*/,
            const std::string& /*continuation_token*/,
            uint32_t /*max_keys*/
        ) -> Result<disk::storage::S3ListObjectsResult> override {
            return disk::storage::S3ListObjectsResult{};
        }

        auto DeleteObjects(const std::vector<std::string>& /*keys*/) -> Result<void> override {
            return {};
        }

        auto CreateMultipartUpload(const std::string& /*key*/) -> Result<std::string> override {
            return "unused-upload";
        }

        auto UploadPart(
            const std::string& /*key*/,
            const std::string& /*upload_id*/,
            int /*part_number*/,
            std::string /*data*/
        ) -> Result<std::string> override {
            return "unused-etag";
        }

        auto UploadPartCopy(
            const std::string& /*source_key*/,
            const std::string& /*destination_key*/,
            const std::string& /*upload_id*/,
            int /*part_number*/,
            uint64_t /*start*/,
            uint64_t /*length*/
        ) -> Result<std::string> override {
            return "unused-etag";
        }

        auto CompleteMultipartUpload(
            const std::string& /*key*/,
            const std::string& /*upload_id*/,
            const std::vector<disk::storage::S3CompletedPart>& /*parts*/,
            bool /*only_if_absent*/
        ) -> Result<disk::storage::S3CompleteMultipartResult> override {
            return disk::storage::S3CompleteMultipartResult{ .created = true };
        }

        auto AbortMultipartUpload(
            const std::string& /*key*/,
            const std::string& /*upload_id*/
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
    EXPECT_NE(std::dynamic_pointer_cast<disk::storage::LocalFileStorage>(bundle.storage), nullptr);
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

    auto storage_backend = std::dynamic_pointer_cast<disk::storage::S3ObjectStorage>(bundle.storage);
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
