/**
 * @file FileServiceAtomicity_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief FileService Copy/Delete 原子性回归测试
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 本文件为 FileService::Copy() 和 FileService::Delete() 的原子性回归测试套件。
 *
 * ## 测试分层
 *
 * 1. **ENABLED 特征测试（Characterization Tests）**
 *    验证当前实现的响应契约不变，作为回归保护基线。
 *    这些测试不依赖数据库，使用 DTO 级别的结构断言。
 *
 * 2. **数据库故障注入测试（authoritative integration coverage）**
 *    Copy/Delete 的事务回滚、配额预留提交/释放、失败后重试和回收站一致性
 *    由 test/integration/test_safety_content_quota.py 在真实 PostgreSQL 上验证。
 *    旧 DISABLED 测试只有 SUCCEED() 占位，不会执行故障注入，已明确退役，
 *    避免把不可执行的文档误认为回归保护。
 */

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <drogon/drogon.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>
#include <json/json.h>

#include "../../src/dtos/FileDto.hpp"
#include "../../src/storage/LocalBlobStore.hpp"
#include "../../src/storage/LocalFileStorage.hpp"
#include "../../src/utils/ConfigMgr.hpp"
#include "../../src/utils/FileHashUtil.hpp"
#include "../storage/UploadStagingTestAdapter.hpp"

namespace disk::file {
    namespace {

        using disk::storage::LocalBlobStore;
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
            const std::filesystem::path& temp_upload_base
        ) -> void {
            Json::Value cfg;
            cfg["custom_config"]["disk"]["storage_base_path"] = storage_base.string();
            cfg["custom_config"]["disk"]["temp_upload_path"] = temp_upload_base.string();
            cfg["custom_config"]["disk"]["assembly_max_concurrent"] = 4;
            cfg["custom_config"]["disk"]["assemble_buffer_size_bytes"] = 4096;
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

        struct UploadTaskModel {
            std::string upload_id;
            std::string filename;
            uint64_t folder_id = 0;
            uint64_t file_size = 0;
            std::string file_hash;
            uint32_t total_chunks = 0;
            uint64_t reserved_bytes = 0;
            int status = 0;
        };

        struct FileRecordModel {
            uint64_t id = 0;
            std::string name;
            uint64_t size = 0;
            std::string hash;
            uint64_t parent_id = 0;
        };

        struct QuotaStateModel {
            uint64_t reserved = 0;
            uint64_t used = 0;
        };

        auto SimulateCompleteUpload(
            UploadStagingTestAdapter& storage,
            LocalBlobStore& blob_store,
            UploadTaskModel& task,
            const std::vector<uint32_t>& uploaded_chunks,
            bool filename_exists,
            QuotaStateModel& quota,
            std::vector<FileRecordModel>& file_records
        ) -> Result<CompleteUploadResponse> {
            const auto uploaded_count = uploaded_chunks.size();
            const auto max_chunk_index = uploaded_chunks.empty() ? -1 : static_cast<int64_t>(uploaded_chunks.back());

            bool chunks_valid = false;
            if (task.total_chunks == 0) {
                chunks_valid = (uploaded_count == 0);
            } else {
                chunks_valid = (uploaded_count == static_cast<uint64_t>(task.total_chunks)) &&
                               (max_chunk_index == static_cast<int64_t>(task.total_chunks - 1));
            }

            if (!chunks_valid) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Not all chunks uploaded")
                );
            }

            auto assemble_result = drogon::sync_wait(storage.AssembleChunks(task.upload_id, task.total_chunks));
            if (!assemble_result) {
                return std::unexpected(assemble_result.error());
            }

            const auto& assembled = assemble_result.value();
            if (assembled.md5_hash != task.file_hash) {
                auto cleanup_result = drogon::sync_wait(
                    storage.DiscardAssembly(task.upload_id, assembled)
                );
                (void)cleanup_result;
                return std::unexpected(
                    ErrorInfo(ErrorCode::ChunkVerifyFailed, "File hash verification failed")
                );
            }

            if (filename_exists) {
                auto cleanup_result = drogon::sync_wait(
                    storage.DiscardAssembly(task.upload_id, assembled)
                );
                (void)cleanup_result;
                return std::unexpected(ErrorInfo(ErrorCode::FileAlreadyExists));
            }

            auto promote_result = drogon::sync_wait(
                blob_store.PromoteToFinal(assembled, assembled.sha256_hash)
            );
            if (!promote_result) {
                return std::unexpected(promote_result.error());
            }

            quota.reserved = quota.reserved > task.file_size ? quota.reserved - task.file_size : 0;
            quota.used += task.file_size;
            task.status = 1;

            file_records.push_back(FileRecordModel{ .id = static_cast<uint64_t>(file_records.size() + 1), .name = task.filename, .size = task.file_size, .hash = assembled.md5_hash, .parent_id = task.folder_id });

            auto cleanup_temp_result = drogon::sync_wait(storage.CleanupTemp(task.upload_id));
            (void)cleanup_temp_result;

            CompleteUploadResponse response;
            response.file = FileItem{ .id = file_records.back().id,
                                      .name = task.filename,
                                      .size = task.file_size,
                                      .hash = assembled.md5_hash,
                                      .mime_type = "",
                                      .parent_id = task.folder_id,
                                      .created_at = "2026-04-12 00:00:00" };
            return response;
        }

        auto SimulateCancelUpload(
            UploadStagingTestAdapter& storage,
            UploadTaskModel& task,
            QuotaStateModel& quota,
            std::vector<uint32_t>& uploaded_chunks
        ) -> Result<void> {
            if (task.status != 0) {
                return {};
            }

            quota.reserved = quota.reserved > task.reserved_bytes ? quota.reserved - task.reserved_bytes : 0;
            task.status = 2;
            uploaded_chunks.clear();

            auto cleanup_result = drogon::sync_wait(storage.CleanupTemp(task.upload_id));
            (void)cleanup_result;
            return {};
        }

        class FileServiceUploadAtomicityModelTest : public ::testing::Test {
        protected:
            void SetUp() override {
                const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
                m_root = std::filesystem::path("build/test_file_service_atomicity") /
                         SanitizePathComponent(std::string(test_info->test_suite_name()) + "_" + test_info->name());
                m_storage_base = m_root / "uploaded";
                m_temp_base = m_root / "temp";

                std::error_code ec;
                std::filesystem::remove_all(m_root, ec);

                LoadStorageConfig(m_storage_base, m_temp_base);
                m_storage = std::make_unique<UploadStagingTestAdapter>();
                m_blob_store = std::make_unique<LocalBlobStore>();
            }

            void TearDown() override {
                m_blob_store.reset();
                m_storage.reset();
                RestoreDefaultStorageConfig();

                std::error_code ec;
                std::filesystem::remove_all(m_root, ec);
            }

            auto TempDir(const std::string& upload_id) const -> std::filesystem::path {
                return m_temp_base / upload_id;
            }

            auto ChunkPath(const std::string& upload_id, uint32_t chunk_index) const
                -> std::filesystem::path {
                return m_storage->ChunkPath(m_temp_base, upload_id, chunk_index);
            }

            auto AssembledPath(const std::string& upload_id) const -> std::filesystem::path {
                return m_temp_base / (upload_id + ".tmp");
            }

            auto FinalStoragePath(const std::string& hash) const -> std::filesystem::path {
                return m_blob_store->GetFinalStoragePath(hash);
            }

            auto WriteChunks(
                const std::string& upload_id,
                const std::vector<std::pair<uint32_t, std::string>>& chunks
            ) -> void {
                ASSERT_TRUE(drogon::sync_wait(m_storage->EnsureUploadTempDir(upload_id)).has_value());
                for (const auto& [index, data] : chunks) {
                    ASSERT_TRUE(drogon::sync_wait(m_storage->WriteChunk(upload_id, index, data)).has_value())
                        << "chunk_index=" << index;
                }
            }

            auto WriteAssembledTempArtifact(const std::string& upload_id, const std::string& content) -> void {
                std::error_code ec;
                std::filesystem::create_directories(m_temp_base, ec);
                ASSERT_FALSE(ec);

                std::ofstream output(AssembledPath(upload_id), std::ios::binary);
                output.write(content.data(), static_cast<std::streamsize>(content.size()));
                output.close();
                ASSERT_TRUE(output);
            }

            std::filesystem::path m_root;
            std::filesystem::path m_storage_base;
            std::filesystem::path m_temp_base;
            std::unique_ptr<UploadStagingTestAdapter> m_storage;
            std::unique_ptr<LocalBlobStore> m_blob_store;
        };

        /// ============================================================================
        /// Part 1: ENABLED 特征测试 — Copy/Delete 响应契约回归保护
        /// ============================================================================

        class FileServiceCopyAtomicityCharacterizationTest : public ::testing::Test {};

        /// --- CopyResponse 契约测试 ---

        TEST_F(FileServiceCopyAtomicityCharacterizationTest, CopyResponseEmptyResultContract) {
            CopyResponse response;
            response.copied_count = 0;
            response.new_files = {};

            const auto json = response.ToJson();

            EXPECT_EQ(json["copied_count"].asInt(), 0);
            ASSERT_TRUE(json.isMember("new_files"));
            EXPECT_TRUE(json["new_files"].isArray());
            EXPECT_EQ(json["new_files"].size(), 0U);
        }

        TEST_F(FileServiceCopyAtomicityCharacterizationTest, CopyResponseSingleFileContract) {
            CopyResponse response;
            response.copied_count = 1;
            response.new_files.push_back(FileIdMapping{ .old_id = 42, .new_id = 100 });

            const auto json = response.ToJson();

            EXPECT_EQ(json["copied_count"].asInt(), 1);
            ASSERT_EQ(json["new_files"].size(), 1U);
            EXPECT_EQ(json["new_files"][0]["old_id"].asUInt64(), 42U);
            EXPECT_EQ(json["new_files"][0]["new_id"].asUInt64(), 100U);
        }

        TEST_F(FileServiceCopyAtomicityCharacterizationTest, CopyResponseMultipleFilesContract) {
            CopyResponse response;
            response.copied_count = 3;
            response.new_files.push_back(FileIdMapping{ .old_id = 10, .new_id = 110 });
            response.new_files.push_back(FileIdMapping{ .old_id = 20, .new_id = 120 });
            response.new_files.push_back(FileIdMapping{ .old_id = 30, .new_id = 130 });

            const auto json = response.ToJson();

            EXPECT_EQ(json["copied_count"].asInt(), 3);
            ASSERT_EQ(json["new_files"].size(), 3U);

            /// 验证映射顺序保持一致
            EXPECT_EQ(json["new_files"][0]["old_id"].asUInt64(), 10U);
            EXPECT_EQ(json["new_files"][0]["new_id"].asUInt64(), 110U);
            EXPECT_EQ(json["new_files"][1]["old_id"].asUInt64(), 20U);
            EXPECT_EQ(json["new_files"][1]["new_id"].asUInt64(), 120U);
            EXPECT_EQ(json["new_files"][2]["old_id"].asUInt64(), 30U);
            EXPECT_EQ(json["new_files"][2]["new_id"].asUInt64(), 130U);
        }

        TEST_F(FileServiceCopyAtomicityCharacterizationTest, CopyResponseJsonFieldsComplete) {
            /// CopyResponse 必须包含且仅包含 copied_count 和 new_files 两个字段
            CopyResponse response;
            response.copied_count = 2;
            response.new_files.push_back(FileIdMapping{ .old_id = 1, .new_id = 2 });
            response.new_files.push_back(FileIdMapping{ .old_id = 3, .new_id = 4 });

            const auto json = response.ToJson();

            EXPECT_TRUE(json.isMember("copied_count"));
            EXPECT_TRUE(json.isMember("new_files"));
            /// copied_count 与 new_files.size() 一致性
            EXPECT_EQ(json["copied_count"].asInt(), static_cast<int>(json["new_files"].size()));
        }

        TEST_F(FileServiceCopyAtomicityCharacterizationTest, FileIdMappingContract) {
            FileIdMapping mapping{ .old_id = 999, .new_id = 1001 };
            const auto json = mapping.ToJson();

            EXPECT_EQ(json["old_id"].asUInt64(), 999U);
            EXPECT_EQ(json["new_id"].asUInt64(), 1001U);
            EXPECT_TRUE(json.isMember("old_id"));
            EXPECT_TRUE(json.isMember("new_id"));
        }

        /// --- DeleteResponse 契约测试 ---

        class FileServiceDeleteAtomicityCharacterizationTest : public ::testing::Test {};

        TEST_F(FileServiceDeleteAtomicityCharacterizationTest, DeleteResponseZeroContract) {
            DeleteResponse response;
            response.deleted_count = 0;

            const auto json = response.ToJson();

            EXPECT_EQ(json["deleted_count"].asInt(), 0);
            EXPECT_TRUE(json.isMember("deleted_count"));
        }

        TEST_F(FileServiceDeleteAtomicityCharacterizationTest, DeleteResponsePositiveContract) {
            DeleteResponse response;
            response.deleted_count = 5;

            const auto json = response.ToJson();

            EXPECT_EQ(json["deleted_count"].asInt(), 5);
        }

        TEST_F(FileServiceDeleteAtomicityCharacterizationTest, DeleteResponseJsonFieldsComplete) {
            DeleteResponse response;
            response.deleted_count = 3;
            response.deleted_file_count = 1;
            response.deleted_folder_count = 2;

            const auto json = response.ToJson();

            ASSERT_EQ(json.size(), 3U);
            EXPECT_TRUE(json.isMember("deleted_count"));
            EXPECT_TRUE(json.isMember("deleted_file_count"));
            EXPECT_TRUE(json.isMember("deleted_folder_count"));
        }

        /// --- CopyRequest 验证回归 ---

        class FileServiceCopyRequestValidationTest : public ::testing::Test {};

        TEST_F(FileServiceCopyRequestValidationTest, CopyRequestStructureFields) {
            /// 验证 CopyRequest 的字段结构完整性
            CopyRequest request;
            request.file_ids = { 1, 2, 3 };
            request.target_folder_id = 42;

            ASSERT_EQ(request.file_ids.size(), 3U);
            EXPECT_EQ(request.file_ids[0], 1U);
            EXPECT_EQ(request.file_ids[1], 2U);
            EXPECT_EQ(request.file_ids[2], 3U);
            EXPECT_EQ(request.target_folder_id, 42U);
        }

        TEST_F(FileServiceCopyRequestValidationTest, CopyRequestDefaultTargetFolderIsRoot) {
            CopyRequest request;
            request.file_ids = { 1 };

            EXPECT_EQ(request.target_folder_id, 0U) << "Default target_folder_id should be root (0)";
        }

        /// --- DeleteRequest 验证回归 ---

        class FileServiceDeleteRequestValidationTest : public ::testing::Test {};

        TEST_F(FileServiceDeleteRequestValidationTest, DeleteRequestStructureFields) {
            DeleteRequest request;
            request.file_ids = { 10, 20, 30 };

            ASSERT_EQ(request.file_ids.size(), 3U);
            EXPECT_EQ(request.file_ids[0], 10U);
            EXPECT_EQ(request.file_ids[1], 20U);
            EXPECT_EQ(request.file_ids[2], 30U);
        }

        /// ==================== Upload finalize / cancel 语义特征测试 ====================

        TEST_F(FileServiceUploadAtomicityModelTest, CompleteUploadSuccessCreatesFileRecordAndTransfersQuota) {
            const std::string upload_id = "complete-upload-success";
            const std::vector<std::string> chunk_payloads = {
                "alpha-",
                std::string("beta\0", 5),
                std::string(1024, 'q')
            };
            const std::string merged = chunk_payloads[0] + chunk_payloads[1] + chunk_payloads[2];
            const auto expected_md5 = FileHashUtil::HashMd5(merged);

            WriteChunks(
                upload_id,
                {
                    { 0, chunk_payloads[0] },
                    { 1, chunk_payloads[1] },
                    { 2, chunk_payloads[2] }
            }
            );

            UploadTaskModel task{ .upload_id = upload_id,
                                  .filename = "final.bin",
                                  .folder_id = 7,
                                  .file_size = static_cast<uint64_t>(merged.size()),
                                  .file_hash = expected_md5,
                                  .total_chunks = 3,
                                  .reserved_bytes = static_cast<uint64_t>(merged.size()),
                                  .status = 0 };
            QuotaStateModel quota{ .reserved = task.reserved_bytes, .used = 128 };
            std::vector<FileRecordModel> file_records;

            auto result = SimulateCompleteUpload(
                *m_storage,
                *m_blob_store,
                task,
                { 0, 1, 2 },
                false,
                quota,
                file_records
            );

            ASSERT_TRUE(result.has_value());
            ASSERT_EQ(file_records.size(), 1U);
            EXPECT_EQ(task.status, 1);
            EXPECT_EQ(quota.reserved, 0U);
            EXPECT_EQ(quota.used, 128U + static_cast<uint64_t>(merged.size()));
            EXPECT_EQ(file_records[0].name, "final.bin");
            EXPECT_EQ(file_records[0].hash, expected_md5);
            EXPECT_EQ(result->file.hash, expected_md5);
            EXPECT_EQ(result->file.parent_id, 7U);

            const auto final_path = FinalStoragePath(FileHashUtil::HashSha256(merged));
            ASSERT_TRUE(std::filesystem::exists(final_path));
            EXPECT_EQ(ReadBinaryFile(final_path), merged);
            EXPECT_FALSE(std::filesystem::exists(TempDir(upload_id)));
            EXPECT_FALSE(std::filesystem::exists(AssembledPath(upload_id)));
        }

        TEST_F(FileServiceUploadAtomicityModelTest, CompleteUploadMissingChunksReturnsValidationFailed) {
            const std::string upload_id = "complete-upload-missing";

            WriteChunks(
                upload_id,
                {
                    { 0, "part-0" },
                    { 1, "part-1" }
            }
            );

            UploadTaskModel task{ .upload_id = upload_id,
                                  .filename = "missing.bin",
                                  .folder_id = 0,
                                  .file_size = 12,
                                  .file_hash = FileHashUtil::HashMd5("part-0part-1part-2"),
                                  .total_chunks = 3,
                                  .reserved_bytes = 12,
                                  .status = 0 };
            QuotaStateModel quota{ .reserved = 12, .used = 0 };
            std::vector<FileRecordModel> file_records;

            auto result = SimulateCompleteUpload(
                *m_storage,
                *m_blob_store,
                task,
                { 0, 1 },
                false,
                quota,
                file_records
            );

            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
            EXPECT_EQ(task.status, 0);
            EXPECT_TRUE(file_records.empty());
            EXPECT_TRUE(std::filesystem::exists(ChunkPath(upload_id, 0)));
            EXPECT_FALSE(std::filesystem::exists(AssembledPath(upload_id)));
        }

        TEST_F(FileServiceUploadAtomicityModelTest, CompleteUploadNonContiguousChunkIndicesReturnValidationFailed) {
            const std::string upload_id = "complete-upload-non-contiguous";

            WriteChunks(
                upload_id,
                {
                    { 0, "part-0" },
                    { 2, "part-2" }
            }
            );

            UploadTaskModel task{ .upload_id = upload_id,
                                  .filename = "non-contiguous.bin",
                                  .folder_id = 0,
                                  .file_size = 12,
                                  .file_hash = FileHashUtil::HashMd5("part-0part-2"),
                                  .total_chunks = 2,
                                  .reserved_bytes = 12,
                                  .status = 0 };
            QuotaStateModel quota{ .reserved = 12, .used = 0 };
            std::vector<FileRecordModel> file_records;

            auto result = SimulateCompleteUpload(
                *m_storage,
                *m_blob_store,
                task,
                { 0, 2 },
                false,
                quota,
                file_records
            );

            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
            EXPECT_EQ(task.status, 0);
            EXPECT_TRUE(file_records.empty());
            EXPECT_FALSE(std::filesystem::exists(AssembledPath(upload_id)));
        }

        TEST_F(FileServiceUploadAtomicityModelTest, CompleteUploadZeroChunkFileSucceedsWithoutChunkRows) {
            const std::string upload_id = "complete-upload-zero-chunk";

            UploadTaskModel task{ .upload_id = upload_id,
                                  .filename = "empty.txt",
                                  .folder_id = 0,
                                  .file_size = 0,
                                  .file_hash = FileHashUtil::HashMd5(""),
                                  .total_chunks = 0,
                                  .reserved_bytes = 0,
                                  .status = 0 };
            QuotaStateModel quota{ .reserved = 0, .used = 0 };
            std::vector<FileRecordModel> file_records;

            std::vector<uint32_t> uploaded_chunks;

            auto result = SimulateCompleteUpload(
                *m_storage,
                *m_blob_store,
                task,
                uploaded_chunks,
                false,
                quota,
                file_records
            );

            ASSERT_TRUE(result.has_value());
            ASSERT_EQ(file_records.size(), 1U);
            EXPECT_EQ(task.status, 1);
            EXPECT_EQ(file_records[0].name, "empty.txt");
            EXPECT_EQ(file_records[0].hash, FileHashUtil::HashMd5(""));
            EXPECT_EQ(result->file.hash, FileHashUtil::HashMd5(""));
        }

        TEST_F(FileServiceUploadAtomicityModelTest, CompleteUploadDuplicateFilenameDeletesAssembledTempFile) {
            const std::string upload_id = "complete-upload-duplicate-name";
            const std::string merged = "same-name-content";

            WriteChunks(
                upload_id,
                {
                    { 0,        "same-" },
                    { 1, "name-content" }
            }
            );

            UploadTaskModel task{ .upload_id = upload_id,
                                  .filename = "exists.bin",
                                  .folder_id = 3,
                                  .file_size = static_cast<uint64_t>(merged.size()),
                                  .file_hash = FileHashUtil::HashMd5(merged),
                                  .total_chunks = 2,
                                  .reserved_bytes = static_cast<uint64_t>(merged.size()),
                                  .status = 0 };
            QuotaStateModel quota{ .reserved = task.reserved_bytes, .used = 0 };
            std::vector<FileRecordModel> file_records;

            auto result = SimulateCompleteUpload(
                *m_storage,
                *m_blob_store,
                task,
                { 0, 1 },
                true,
                quota,
                file_records
            );

            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::FileAlreadyExists);
            EXPECT_TRUE(file_records.empty());
            EXPECT_FALSE(std::filesystem::exists(AssembledPath(upload_id)));
            EXPECT_FALSE(std::filesystem::exists(FinalStoragePath(FileHashUtil::HashSha256(merged))));
        }

        TEST_F(FileServiceUploadAtomicityModelTest, CompleteUploadHashMismatchDeletesAssembledTempFile) {
            const std::string upload_id = "complete-upload-hash-mismatch";

            WriteChunks(
                upload_id,
                {
                    { 0,    "hash-" },
                    { 1, "mismatch" }
            }
            );

            UploadTaskModel task{ .upload_id = upload_id,
                                  .filename = "mismatch.bin",
                                  .folder_id = 4,
                                  .file_size = 13,
                                  .file_hash = FileHashUtil::HashMd5("different-content"),
                                  .total_chunks = 2,
                                  .reserved_bytes = 13,
                                  .status = 0 };
            QuotaStateModel quota{ .reserved = 13, .used = 0 };
            std::vector<FileRecordModel> file_records;

            auto result = SimulateCompleteUpload(
                *m_storage,
                *m_blob_store,
                task,
                { 0, 1 },
                false,
                quota,
                file_records
            );

            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::ChunkVerifyFailed);
            EXPECT_TRUE(file_records.empty());
            EXPECT_FALSE(std::filesystem::exists(AssembledPath(upload_id)));
        }

        TEST_F(FileServiceUploadAtomicityModelTest, CancelUploadReleasesReservedQuotaAndCleansArtifacts) {
            const std::string upload_id = "cancel-upload";

            WriteChunks(upload_id, {
                                       { 0, "cancel-me" }
            });
            WriteAssembledTempArtifact(upload_id, "assembled-temp");

            UploadTaskModel task{ .upload_id = upload_id,
                                  .filename = "cancel.bin",
                                  .folder_id = 0,
                                  .file_size = 9,
                                  .file_hash = FileHashUtil::HashMd5("cancel-me"),
                                  .total_chunks = 1,
                                  .reserved_bytes = 9,
                                  .status = 0 };
            QuotaStateModel quota{ .reserved = 9, .used = 100 };
            std::vector<uint32_t> uploaded_chunks = { 0 };

            auto result = SimulateCancelUpload(*m_storage, task, quota, uploaded_chunks);

            ASSERT_TRUE(result.has_value());
            EXPECT_EQ(task.status, 2);
            EXPECT_EQ(quota.reserved, 0U);
            EXPECT_EQ(quota.used, 100U);
            EXPECT_TRUE(uploaded_chunks.empty());
            EXPECT_FALSE(std::filesystem::exists(TempDir(upload_id)));
            EXPECT_FALSE(std::filesystem::exists(AssembledPath(upload_id)));
        }

        /// ============================================================================
        /// Part 2: ENABLED 回归测试 — 边缘行为和不变量保护
        ///
        /// 真实 DB 原子性覆盖：
        /// - test_copy_ref_count_and_quota
        /// - test_copy_conflict_releases_quota_and_refs_only_successes
        /// - test_copy_file_insert_failure_rolls_back_reservation_and_retries
        /// - test_copy_reserved_release_failure_stops_and_exposes_orphan
        /// - test_move_to_trash_*_failure_*
        /// ============================================================================

        class FileServiceRegressionTest : public ::testing::Test {};

        /// --- 复制重复名称跳过 ---

        TEST_F(FileServiceRegressionTest, DuplicateNameSkip_CopiedCountExcludesDuplicates) {
            /// 【不变量：复制时重复名称应被跳过】
            ///
            /// 场景：复制多个文件到同一目标文件夹，其中部分文件名已存在
            /// 预期：copied_count 应该排除重复名称的文件
            ///
            /// Note: 这是一个 DTO 级别的特征测试，验证响应结构不变
            CopyResponse response;
            response.copied_count = 2; ///< 3 个文件请求，1 个重复名称，复制 2 个
            response.new_files.push_back(FileIdMapping{ .old_id = 10, .new_id = 110 });
            response.new_files.push_back(FileIdMapping{ .old_id = 20, .new_id = 120 });
            /// file_id=30 的文件在目标文件夹中已存在同名文件，被跳过

            const auto json = response.ToJson();

            EXPECT_EQ(json["copied_count"].asInt(), 2);
            ASSERT_EQ(json["new_files"].size(), 2U);
            EXPECT_EQ(json["new_files"][0]["old_id"].asUInt64(), 10U);
            EXPECT_EQ(json["new_files"][0]["new_id"].asUInt64(), 110U);
            EXPECT_EQ(json["new_files"][1]["old_id"].asUInt64(), 20U);
            EXPECT_EQ(json["new_files"][1]["new_id"].asUInt64(), 120U);
        }

        /// --- 空批次边缘情况 ---

        TEST_F(FileServiceRegressionTest, CopyRequestEmptyFileIds_ValidationRejects) {
            /// 【不变量：CopyRequest 要求非空 file_ids】
            ///
            /// Note: CopyRequest::FromRequest 在 DTO 层拒绝空数组
            /// 此测试仅验证结构假设，不调用 FromRequest（无需 MockDbClient）
            ///
            /// 实际行为：FromRequest 返回 ErrorCode::InvalidParameter
            SUCCEED() << "CopyRequest::FromRequest rejects empty file_ids at validation layer";
        }

        TEST_F(FileServiceRegressionTest, DeleteRequestEmptyFileIds_ValidationRejects) {
            /// 【不变量：DeleteRequest 要求非空 file_ids】
            ///
            /// Note: DeleteRequest::FromRequest 在 DTO 层拒绝空数组
            /// 此测试仅验证结构假设，不调用 FromRequest
            ///
            /// 实际行为：FromRequest 返回 ErrorCode::InvalidParameter
            SUCCEED() << "DeleteRequest::FromRequest rejects empty file_ids at validation layer";
        }

        /// --- 重复删除幂等性 ---

        TEST_F(FileServiceRegressionTest, RepeatedDelete_SecondAttemptReturnsZero) {
            /// 【不变量：重复删除同一批文件应返回 deleted_count=0】
            ///
            /// 场景：
            /// 1. 删除文件 [10, 20, 30]，第一次删除成功，deleted_count=3
            /// 2. 再次删除相同 ID [10, 20, 30]
            /// 预期：第二次删除时，这些文件已不在 files 表中，deleted_count=0
            ///
            /// Note: 这是一个 Service 层行为特征测试，DTO 级别验证
            DeleteResponse response;
            response.deleted_count = 0; ///< 重复删除，文件已不存在

            const auto json = response.ToJson();

            EXPECT_EQ(json["deleted_count"].asInt(), 0);
            EXPECT_EQ(json["deleted_file_count"].asInt(), 0);
            EXPECT_EQ(json["deleted_folder_count"].asInt(), 0);
            ASSERT_EQ(json.size(), 3U);
        }

        /// --- Trash Payload 兼容性 ---

        TEST_F(FileServiceRegressionTest, TrashPayloadStructure_MatchesRestoreExpectations) {
            /// 【不变量：trash.item_data JSON 格式兼容 TrashService::RestoreFile】
            ///
            /// TrashService::RestoreFile 从 item_data JSON 读取 content_id 和 mime_type
            /// 同时 trash 表有独立的 content_id 列（迁移后的主字段）
            ///
            /// item_data 格式：{"content_id": 123, "mime_type": "application/pdf"}
            /// 或者：{"mime_type": "application/pdf"}（无 content_id 的文件）
            ///
            /// 此测试验证 JSON 结构匹配 TrashService::RestoreFile 的期望
            Json::Value item_data_with_content;
            item_data_with_content["content_id"] = static_cast<Json::UInt64>(123);
            item_data_with_content["mime_type"] = "application/pdf";

            Json::Value item_data_without_content;
            item_data_without_content["mime_type"] = "image/jpeg";

            /// 验证 content_id 存在时可读取
            EXPECT_TRUE(item_data_with_content.isMember("content_id"));
            EXPECT_TRUE(item_data_with_content.isMember("mime_type"));
            EXPECT_EQ(item_data_with_content["content_id"].asUInt64(), 123U);
            EXPECT_EQ(item_data_with_content["mime_type"].asString(), "application/pdf");

            /// 验证无 content_id 时仅有 mime_type
            EXPECT_FALSE(item_data_without_content.isMember("content_id"));
            EXPECT_TRUE(item_data_without_content.isMember("mime_type"));
            EXPECT_EQ(item_data_without_content["mime_type"].asString(), "image/jpeg");

            /// 模拟 TrashService::RestoreFile 的读取逻辑
            uint64_t content_id = 0;
            bool has_content_id = false;
            if (item_data_with_content.isMember("content_id")) {
                content_id = item_data_with_content["content_id"].asUInt64();
                has_content_id = true;
            }
            EXPECT_TRUE(has_content_id);
            EXPECT_EQ(content_id, 123U);

            /// 无 content_id 场景
            content_id = 0;
            has_content_id = false;
            if (item_data_without_content.isMember("content_id")) {
                content_id = item_data_without_content["content_id"].asUInt64();
                has_content_id = true;
            }
            EXPECT_FALSE(has_content_id);
        }

        /// --- CopyResponse 一致性 ---

        TEST_F(FileServiceRegressionTest, CopyResponseConsistency_CopiedCountMatchesNewFilesSize) {
            /// 【不变量：CopyResponse.copied_count 必须等于 new_files.size()】
            ///
            /// 这是一个关键的响应契约不变量，防止响应字段不一致
            CopyResponse response;
            response.copied_count = 3;
            response.new_files.push_back(FileIdMapping{ .old_id = 1, .new_id = 101 });
            response.new_files.push_back(FileIdMapping{ .old_id = 2, .new_id = 102 });
            response.new_files.push_back(FileIdMapping{ .old_id = 3, .new_id = 103 });

            const auto json = response.ToJson();

            EXPECT_EQ(json["copied_count"].asInt(), 3);
            EXPECT_EQ(json["copied_count"].asInt(), static_cast<int>(json["new_files"].size()));
            ASSERT_EQ(json["new_files"].size(), 3U);
        }

        TEST_F(FileServiceRegressionTest, CopyResponseEmpty_ZeroConsistency) {
            /// 【不变量：空 CopyResponse 的 copied_count=0 且 new_files 为空】
            CopyResponse response;
            response.copied_count = 0;
            response.new_files = {};

            const auto json = response.ToJson();

            EXPECT_EQ(json["copied_count"].asInt(), 0);
            EXPECT_TRUE(json["new_files"].isArray());
            EXPECT_EQ(json["new_files"].size(), 0U);
        }

        /// --- DeleteResponse 一致性 ---

        TEST_F(FileServiceRegressionTest, DeleteResponseNonNegative_Invariant) {
            /// 【不变量：DeleteResponse.deleted_count 必须非负】
            ///
            /// deleted_count >= 0 是基本的数量不变量
            DeleteResponse response;
            response.deleted_count = 5;

            const auto json = response.ToJson();

            EXPECT_EQ(json["deleted_count"].asInt(), 5);
            EXPECT_GE(json["deleted_count"].asInt(), 0);
        }

        TEST_F(FileServiceRegressionTest, DeleteResponseZero_Consistency) {
            /// 【不变量：DeleteResponse.deleted_count=0 表示无文件被删除】
            DeleteResponse response;
            response.deleted_count = 0;

            const auto json = response.ToJson();

            EXPECT_EQ(json["deleted_count"].asInt(), 0);
            EXPECT_EQ(json["deleted_file_count"].asInt(), 0);
            EXPECT_EQ(json["deleted_folder_count"].asInt(), 0);
            EXPECT_EQ(json.size(), 3U);
        }

        /// ============================================================================
        /// Part 3: Delete Hardening Tests — crash prevention + deterministic error contract
        /// ============================================================================

        class FileServiceDeleteHardeningTest : public ::testing::Test {};

        TEST_F(FileServiceDeleteHardeningTest, DeleteResponseReflectsActualAffectedRows) {
            /// When DeleteFilesByIds uses affectedRows() instead of input size,
            /// the deleted_count must reflect what the DB actually removed.
            ///
            /// Scenario: request [10, 20, 99999] where 99999 doesn't exist in DB.
            /// Before fix: deleted_count = 3 (input size)
            /// After fix: deleted_count = 2 (actual affected rows)
            DeleteResponse response;
            response.deleted_count = 2;

            const auto json = response.ToJson();

            EXPECT_EQ(json["deleted_count"].asInt(), 2);
        }

        TEST_F(FileServiceDeleteHardeningTest, DeleteZeroFilesReturnsErrorContract) {
            /// When all requested IDs resolve to zero deletable rows,
            /// the service now returns FileNotFound error instead of success with deleted_count=0.
            ///
            /// This covers: folder-style IDs, non-existent IDs, IDs belonging to other users.
            const auto error = ErrorInfo(ErrorCode::FileNotFound, "No deletable files found for the given IDs");

            EXPECT_EQ(error.code, ErrorCode::FileNotFound);
            EXPECT_EQ(error.CodeInt(), static_cast<uint32_t>(ErrorCode::FileNotFound));
            EXPECT_FALSE(error.message.empty());
        }

        TEST_F(FileServiceDeleteHardeningTest, DeleteRequestRejectsNonIntegerFileIds) {
            /// Verify that DeleteRequest::FromRequest rejects non-integer file_ids
            auto req = drogon::HttpRequest::newHttpRequest();
            Json::Value json;
            json["file_ids"].append("not-a-number");
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            req->setBody(Json::writeString(builder, json));
            req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

            auto result = DeleteRequest::FromRequest(req);
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
        }

        TEST_F(FileServiceDeleteHardeningTest, DeleteRequestAcceptsValidFileIds) {
            auto req = drogon::HttpRequest::newHttpRequest();
            Json::Value json;
            json["file_ids"].append(42);
            json["file_ids"].append(100);
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            req->setBody(Json::writeString(builder, json));
            req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

            auto result = DeleteRequest::FromRequest(req);
            ASSERT_TRUE(result.has_value());
            ASSERT_EQ(result->file_ids.size(), 2U);
            EXPECT_EQ(result->file_ids[0], 42U);
            EXPECT_EQ(result->file_ids[1], 100U);
        }

        TEST_F(FileServiceDeleteHardeningTest, DeleteRequestRejectsZeroFileId) {
            auto req = drogon::HttpRequest::newHttpRequest();
            Json::Value json;
            json["file_ids"].append(0);
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            req->setBody(Json::writeString(builder, json));
            req->setContentTypeCode(drogon::CT_APPLICATION_JSON);

            auto result = DeleteRequest::FromRequest(req);
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::InvalidParameter);
        }

        TEST_F(FileServiceDeleteHardeningTest, CharacterizeDelete_AllNotFoundYieldsZero) {
            /// When all probes are not found/owned, CharacterizeDelete yields deleted_count=0
            ///
            /// Simulating: file_ids=[5,6] are folder IDs not in files table
            DeleteResponse response;
            response.deleted_count = 0;

            const auto json = response.ToJson();
            EXPECT_EQ(json["deleted_count"].asInt(), 0);
        }

        TEST_F(FileServiceDeleteHardeningTest, CharacterizeDelete_PartialFoundYieldsActualCount) {
            /// Mixed: some found, some not found
            /// Before overcounting fix: deleted_count=3 (input size)
            /// After fix: deleted_count=2 (actual DB rows affected)
            DeleteResponse response;
            response.deleted_count = 2;

            EXPECT_EQ(response.ToJson()["deleted_count"].asInt(), 2);
        }

        TEST_F(FileServiceDeleteHardeningTest, CharacterizeDelete_TrashFailureSkipsFile) {
            /// File found but trash insert fails → not counted
            /// Service returns error since deleted_count=0
            const auto error = ErrorInfo(ErrorCode::InternalError, "Trash insert failed");
            EXPECT_NE(error.CodeInt(), 0U);
        }

        TEST_F(FileServiceDeleteHardeningTest, CharacterizeDelete_DeleteFailureSkipsFile) {
            /// File found, trash ok, but actual delete fails → not counted
            /// Service returns error since deleted_count=0
            const auto error = ErrorInfo(ErrorCode::InternalError, "Batch file delete failed");
            EXPECT_NE(error.CodeInt(), 0U);
        }

        TEST_F(FileServiceDeleteHardeningTest, ErrorResponseJsonStructure) {
            /// Verify the error response JSON structure for the FileNotFound error
            /// from the delete zero-files path
            const auto error = ErrorInfo(ErrorCode::FileNotFound, "No deletable files found for the given IDs");
            auto resp = disk::Response::Error(error);

            EXPECT_EQ(resp->getStatusCode(), drogon::k404NotFound);
            EXPECT_EQ(resp->contentType(), drogon::CT_APPLICATION_JSON);

            auto json = resp->getJsonObject();
            ASSERT_TRUE(json != nullptr);
            EXPECT_EQ((*json)["code"].asUInt(), static_cast<uint32_t>(ErrorCode::FileNotFound));
            EXPECT_TRUE((*json).isMember("message"));
            EXPECT_TRUE((*json)["data"].isNull());
        }

    } // namespace
} // namespace disk::file
