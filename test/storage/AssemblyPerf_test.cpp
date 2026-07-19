/**
 * @file AssemblyPerf_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Assembly performance test: multi-chunk upload + assembly timing + hash integrity
 * @copyright Copyright (c) 2026
 *
 * Uses LocalFileStorage symbols provided by FileUploadConsistency_test.cpp
 * (which #includes LocalFileStorage.cpp into the disk-test target).
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <drogon/drogon.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>
#include <sodium/crypto_hash_sha256.h>

#include "../../src/storage/LocalFileStorage.hpp"
#include "../../src/utils/ConfigMgr.hpp"
#include "../../src/utils/FileHashUtil.hpp"
#include "UploadStagingTestAdapter.hpp"

namespace disk::storage {
    namespace {

        using disk::test_support::UploadStagingTestAdapter;
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
            const std::filesystem::path& temp_upload_base,
            uint32_t buffer_size
        ) -> void {
            Json::Value cfg;
            cfg["custom_config"]["disk"]["storage_base_path"] = storage_base.string();
            cfg["custom_config"]["disk"]["temp_upload_path"] = temp_upload_base.string();
            cfg["custom_config"]["disk"]["assembly_max_concurrent"] = 4;
            cfg["custom_config"]["disk"]["assemble_buffer_size_bytes"] = buffer_size;
            drogon::app().loadConfigJson(cfg);
            ConfigMgr::GetInstance()->LoadConfig();
        }

        auto RestoreDefaultStorageConfig() -> void {
            LoadStorageConfig("build/uploaded", "build/temp_uploads", 1048576);
        }

        auto GenerateRandomData(size_t size, uint32_t seed) -> std::string {
            std::string data(size, '\0');
            std::mt19937 gen(seed);
            std::uniform_int_distribution<uint32_t> dist(0, 255);
            for (size_t i = 0; i < size; ++i) {
                data[i] = static_cast<char>(dist(gen));
            }
            return data;
        }

        auto ComputeExpectedMd5(const std::vector<std::string>& chunks) -> std::string {
            std::string combined;
            for (const auto& chunk : chunks) {
                combined += chunk;
            }
            return FileHashUtil::HashMd5(combined);
        }

        auto ComputeExpectedSha256(const std::vector<std::string>& chunks) -> std::string {
            std::string combined;
            for (const auto& chunk : chunks) {
                combined += chunk;
            }
            return FileHashUtil::HashSha256(combined);
        }

        class AssemblyPerfTest : public ::testing::Test {
        protected:
            void SetUp() override {
                const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
                m_root = std::filesystem::path("build/test_assembly_perf") /
                         SanitizePathComponent(std::string(test_info->test_suite_name()) + "_" + test_info->name());
                m_storage_base = m_root / "uploaded";
                m_temp_base = m_root / "temp";

                std::error_code ec;
                std::filesystem::remove_all(m_root, ec);

                LoadStorageConfig(m_storage_base, m_temp_base, 1048576);
                m_storage = std::make_unique<UploadStagingTestAdapter>();
            }

            void TearDown() override {
                m_storage.reset();
                RestoreDefaultStorageConfig();

                std::error_code ec;
                std::filesystem::remove_all(m_root, ec);
            }

            std::filesystem::path m_root;
            std::filesystem::path m_storage_base;
            std::filesystem::path m_temp_base;
            std::unique_ptr<UploadStagingTestAdapter> m_storage;
        };

        TEST_F(AssemblyPerfTest, FourChunksFiveMbAssemblesWithCorrectHashes) {
            constexpr uint32_t kChunkCount = 4;
            constexpr size_t kChunkSize = 5 * 1024 * 1024;
            constexpr size_t kTotalSize = kChunkCount * kChunkSize;
            const std::string upload_id = "perf-4x5mb";

            std::vector<std::string> chunks;
            chunks.reserve(kChunkCount);
            for (uint32_t i = 0; i < kChunkCount; ++i) {
                chunks.push_back(GenerateRandomData(kChunkSize, /*seed=*/42 + i));
            }

            const auto expected_md5 = ComputeExpectedMd5(chunks);
            const auto expected_sha256 = ComputeExpectedSha256(chunks);

            ASSERT_TRUE(drogon::sync_wait(m_storage->EnsureUploadTempDir(upload_id)).has_value());

            for (uint32_t i = 0; i < kChunkCount; ++i) {
                auto write_result =
                    drogon::sync_wait(m_storage->WriteChunk(upload_id, i, std::move(chunks[i])));
                ASSERT_TRUE(write_result.has_value()) << "WriteChunk failed for index " << i;
            }

            auto assemble_start = std::chrono::steady_clock::now();

            auto assemble_result =
                drogon::sync_wait(m_storage->AssembleChunks(upload_id, kChunkCount));

            auto assemble_end = std::chrono::steady_clock::now();
            auto assemble_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   assemble_end - assemble_start
            )
                                   .count();

            ASSERT_TRUE(assemble_result.has_value()) << "AssembleChunks failed";

            const auto& assembled = assemble_result.value();

            EXPECT_EQ(assembled.md5_hash, expected_md5)
                << "MD5 mismatch: expected " << expected_md5 << " got " << assembled.md5_hash;
            EXPECT_EQ(assembled.sha256_hash, expected_sha256)
                << "SHA256 mismatch: expected " << expected_sha256 << " got " << assembled.sha256_hash;
            EXPECT_EQ(assembled.size_bytes, kTotalSize);

            ASSERT_TRUE(std::filesystem::exists(assembled.locator));
            EXPECT_EQ(std::filesystem::file_size(assembled.locator), static_cast<uintmax_t>(kTotalSize));

            EXPECT_GT(assemble_ms, 0)
                << "Assembly of 20MB completed in " << assemble_ms << "ms";

            auto throughput_mbps = (static_cast<double>(kTotalSize) / (1024.0 * 1024.0)) /
                                   (static_cast<double>(assemble_ms) / 1000.0);
            EXPECT_GT(throughput_mbps, 1.0)
                << "Assembly throughput: " << throughput_mbps << " MB/s";
        }

        TEST_F(AssemblyPerfTest, SingleLargeChunkAssemblesWithCorrectHashes) {
            constexpr size_t kDataSize = 20 * 1024 * 1024;
            const std::string upload_id = "perf-single-20mb";

            auto chunk_data = GenerateRandomData(kDataSize, /*seed=*/99);
            const auto expected_md5 = FileHashUtil::HashMd5(chunk_data);
            const auto expected_sha256 = FileHashUtil::HashSha256(chunk_data);

            ASSERT_TRUE(drogon::sync_wait(m_storage->EnsureUploadTempDir(upload_id)).has_value());

            auto write_result =
                drogon::sync_wait(m_storage->WriteChunk(upload_id, 0, std::move(chunk_data)));
            ASSERT_TRUE(write_result.has_value());

            auto assemble_result = drogon::sync_wait(m_storage->AssembleChunks(upload_id, 1));
            ASSERT_TRUE(assemble_result.has_value());

            const auto& assembled = assemble_result.value();
            EXPECT_EQ(assembled.md5_hash, expected_md5);
            EXPECT_EQ(assembled.sha256_hash, expected_sha256);
            EXPECT_EQ(assembled.size_bytes, kDataSize);
            EXPECT_EQ(std::filesystem::file_size(assembled.locator), static_cast<uintmax_t>(kDataSize));
        }

        TEST_F(AssemblyPerfTest, EightChunksFiveMbAssemblesWithCorrectHashes) {
            constexpr uint32_t kChunkCount = 8;
            constexpr size_t kChunkSize = 5 * 1024 * 1024;
            constexpr size_t kTotalSize = kChunkCount * kChunkSize;
            const std::string upload_id = "perf-8x5mb";

            std::vector<std::string> chunks;
            chunks.reserve(kChunkCount);
            for (uint32_t i = 0; i < kChunkCount; ++i) {
                chunks.push_back(GenerateRandomData(kChunkSize, /*seed=*/100 + i));
            }

            const auto expected_md5 = ComputeExpectedMd5(chunks);
            const auto expected_sha256 = ComputeExpectedSha256(chunks);

            ASSERT_TRUE(drogon::sync_wait(m_storage->EnsureUploadTempDir(upload_id)).has_value());

            for (uint32_t i = 0; i < kChunkCount; ++i) {
                auto write_result =
                    drogon::sync_wait(m_storage->WriteChunk(upload_id, i, std::move(chunks[i])));
                ASSERT_TRUE(write_result.has_value()) << "WriteChunk failed for index " << i;
            }

            auto assemble_start = std::chrono::steady_clock::now();

            auto assemble_result =
                drogon::sync_wait(m_storage->AssembleChunks(upload_id, kChunkCount));

            auto assemble_end = std::chrono::steady_clock::now();
            auto assemble_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   assemble_end - assemble_start
            )
                                   .count();

            ASSERT_TRUE(assemble_result.has_value());

            const auto& assembled = assemble_result.value();
            EXPECT_EQ(assembled.md5_hash, expected_md5);
            EXPECT_EQ(assembled.sha256_hash, expected_sha256);
            EXPECT_EQ(assembled.size_bytes, kTotalSize);
            EXPECT_EQ(std::filesystem::file_size(assembled.locator), static_cast<uintmax_t>(kTotalSize));

            auto throughput_mbps = (static_cast<double>(kTotalSize) / (1024.0 * 1024.0)) /
                                   (static_cast<double>(assemble_ms) / 1000.0);
            EXPECT_GT(throughput_mbps, 1.0)
                << "Assembly throughput: " << throughput_mbps << " MB/s";
        }

    } // namespace
} // namespace disk::storage
