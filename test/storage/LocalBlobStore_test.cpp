/**
 * @file LocalBlobStore_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 本地最终内容 Blob 存储测试
 *
 * @copyright Copyright (c) 2026
 */

#include "storage/LocalBlobStore.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include <drogon/drogon.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>
#include <json/json.h>

#include "utils/ConfigMgr.hpp"
#include "utils/FileHashUtil.hpp"

namespace disk::storage {
    namespace {

        using disk::utils::ConfigMgr;
        using disk::utils::FileHashUtil;

        auto SanitizePathComponent(std::string value) -> std::string {
            for (auto& ch : value) {
                const auto is_alpha_num = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                                          (ch >= '0' && ch <= '9');
                if (!is_alpha_num) {
                    ch = '_';
                }
            }
            return value;
        }

        auto LoadStorageConfig(
            const std::filesystem::path& storage_base,
            const std::filesystem::path& temp_upload_base
        ) -> void {
            Json::Value cfg;
            cfg["custom_config"]["disk"]["storage_base_path"] = storage_base.string();
            cfg["custom_config"]["disk"]["temp_upload_path"] = temp_upload_base.string();
            drogon::app().loadConfigJson(cfg);
            ConfigMgr::GetInstance()->LoadConfig();
        }

        auto RestoreDefaultStorageConfig() -> void {
            LoadStorageConfig("build/uploaded", "build/temp_uploads");
        }

        auto ReadBinaryFile(const std::filesystem::path& path) -> std::string {
            std::ifstream input(path, std::ios::binary);
            return {
                std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>()
            };
        }

        auto WriteBinaryFile(const std::filesystem::path& path, const std::string& content) -> void {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            ASSERT_FALSE(ec);

            std::ofstream output(path, std::ios::binary);
            output.write(content.data(), static_cast<std::streamsize>(content.size()));
            output.close();
            ASSERT_TRUE(output);
        }

        auto LocalAssembly(
            const std::filesystem::path& path,
            const std::string& content
        ) -> UploadStagingAssembly {
            return UploadStagingAssembly{
                .backend = UploadStagingBackend::Local,
                .locator = path.string(),
                .size_bytes = content.size(),
                .md5_hash = FileHashUtil::HashMd5(content),
                .sha256_hash = FileHashUtil::HashSha256(content),
            };
        }

        class LocalBlobStoreTest : public ::testing::Test {
        protected:
            void SetUp() override {
                const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
                m_root = std::filesystem::path("build/test_local_blob_store") /
                         SanitizePathComponent(std::string(test_info->test_suite_name()) + "_" + test_info->name());
                m_storage_base = m_root / "uploaded";
                m_temp_base = m_root / "temp";

                std::error_code ec;
                std::filesystem::remove_all(m_root, ec);

                LoadStorageConfig(m_storage_base, m_temp_base);
                m_blob_store = std::make_unique<LocalBlobStore>();
            }

            void TearDown() override {
                m_blob_store.reset();
                RestoreDefaultStorageConfig();

                std::error_code ec;
                std::filesystem::remove_all(m_root, ec);
            }

            auto TempPath(const std::string& name) const -> std::filesystem::path {
                return m_temp_base / name;
            }

            std::filesystem::path m_root;
            std::filesystem::path m_storage_base;
            std::filesystem::path m_temp_base;
            std::unique_ptr<LocalBlobStore> m_blob_store;
        };

        TEST_F(LocalBlobStoreTest, PromoteReadAndDeleteFinalBlob) {
            const std::string content = "promote-read-delete-content";
            const auto hash = FileHashUtil::HashSha256(content);
            const auto temp_path = TempPath("assembled.tmp");
            WriteBinaryFile(temp_path, content);

            auto promote_result = drogon::sync_wait(
                m_blob_store->PromoteToFinal(LocalAssembly(temp_path, content), hash)
            );

            ASSERT_TRUE(promote_result.has_value());
            EXPECT_TRUE(promote_result->created);
            EXPECT_TRUE(std::filesystem::exists(temp_path));
            ASSERT_TRUE(std::filesystem::exists(promote_result->path));
            EXPECT_EQ(ReadBinaryFile(promote_result->path), content);

            auto open_result = drogon::sync_wait(m_blob_store->OpenForRead(promote_result->path));
            ASSERT_TRUE(open_result.has_value());
            std::string read_content{
                std::istreambuf_iterator<char>(**open_result),
                std::istreambuf_iterator<char>()
            };
            EXPECT_EQ(read_content, content);

            auto delete_result = drogon::sync_wait(m_blob_store->DeleteBlob(promote_result->path));
            ASSERT_TRUE(delete_result.has_value());
            EXPECT_FALSE(std::filesystem::exists(promote_result->path));
        }

        TEST_F(LocalBlobStoreTest, PromoteReusesExistingBlobAndPreservesContent) {
            const std::string content = "existing-final-content";
            const auto hash = FileHashUtil::HashSha256(content);
            const auto final_path = m_blob_store->GetFinalStoragePath(hash);
            const auto temp_path = TempPath("dedup.tmp");

            WriteBinaryFile(final_path, content);
            WriteBinaryFile(temp_path, content);

            auto promote_result = drogon::sync_wait(
                m_blob_store->PromoteToFinal(LocalAssembly(temp_path, content), hash)
            );

            ASSERT_TRUE(promote_result.has_value());
            EXPECT_FALSE(promote_result->created);
            EXPECT_EQ(promote_result->path, final_path);
            EXPECT_TRUE(std::filesystem::exists(temp_path));
            ASSERT_TRUE(std::filesystem::exists(final_path));
            EXPECT_EQ(ReadBinaryFile(final_path), content);
        }

        TEST_F(LocalBlobStoreTest, PromoteRejectsCorruptExistingBlob) {
            const std::string content = "expected-final-content";
            const auto hash = FileHashUtil::HashSha256(content);
            const auto final_path = m_blob_store->GetFinalStoragePath(hash);
            const auto temp_path = TempPath("conflict.tmp");
            WriteBinaryFile(final_path, "corrupt-final-content");
            WriteBinaryFile(temp_path, content);

            auto promote_result = drogon::sync_wait(
                m_blob_store->PromoteToFinal(LocalAssembly(temp_path, content), hash)
            );

            ASSERT_FALSE(promote_result.has_value());
            EXPECT_EQ(promote_result.error().code, ErrorCode::ChunkVerifyFailed);
            EXPECT_EQ(ReadBinaryFile(final_path), "corrupt-final-content");
            EXPECT_TRUE(std::filesystem::exists(temp_path));
        }

        TEST_F(LocalBlobStoreTest, DeleteBlobIsIdempotent) {
            const std::string content = "delete-idempotent";
            const auto hash = FileHashUtil::HashSha256(content);
            const auto final_path = m_blob_store->GetFinalStoragePath(hash);
            WriteBinaryFile(final_path, content);

            auto first_result = drogon::sync_wait(m_blob_store->DeleteBlob(final_path));
            auto second_result = drogon::sync_wait(m_blob_store->DeleteBlob(final_path));

            ASSERT_TRUE(first_result.has_value());
            ASSERT_TRUE(second_result.has_value());
            EXPECT_FALSE(std::filesystem::exists(final_path));
        }

        TEST_F(LocalBlobStoreTest, GetFinalStoragePathUsesSha256Namespace) {
            const std::string hash = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

            const auto final_path = m_blob_store->GetFinalStoragePath(hash);

            EXPECT_EQ(final_path, m_storage_base / "sha256" / "01" / (hash + ".bin"));
        }

        TEST_F(LocalBlobStoreTest, DownloadPathUsesPersistedStoragePath) {
            const auto legacy_path = m_storage_base / "ab" / "legacy-md5.bin";
            const BlobDescriptor blob{
                .content_id = 1,
                .hash_md5 = "00000000000000000000000000000000",
                .storage_path = legacy_path.string(),
                .size = 10,
            };

            auto resolved = m_blob_store->GetLocalBlobPathForDownload(blob);

            ASSERT_TRUE(resolved.has_value());
            EXPECT_EQ(resolved.value(), legacy_path);
        }

    } // namespace
} // namespace disk::storage
