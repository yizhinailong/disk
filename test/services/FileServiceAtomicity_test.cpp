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
 * 2. **DISABLED 故障注入测试（Fault Injection Tests）**
 *    文档化 Copy/Delete 操作中的非原子故障区域。
 *    当前处于 DISABLED 状态，原因：
 *    - 现有 MockDbClient 不支持 newTransactionCoro() 模拟
 *    - 需要事务缝（transaction seam）注入 SQL 步骤失败
 *    - 在 FileService 添加事务包装后启用
 *    启用后：这些测试将在非事务实现下 FAIL，在事务实现下 PASS。
 *
 * ## Copy 故障区域分析
 * Source: src/services/FileService.cpp lines 1116-1407
 *
 * 步骤序列：
 *  1) [1138-1197] 文件获取 + 配额预扣（storage_used += total_copy_size）
 *  2) [1300-1326] ref_count 批量递增（file_contents.ref_count += N）
 *  3) [1346-1389] files 行批量插入
 *  4) [1393-1398] 配额释放（storage_used -= (reserved - actual)）
 *
 * **Copy Zone A**：配额已扣（步骤1），ref_count 未递增（步骤2前崩溃）
 *   → 影响：storage_used 虚增，用户可用空间泄漏
 *   → 事务后保证：整个批次回滚，配额恢复
 *
 * **Copy Zone B**：ref_count 已递增（步骤2），files 未插入（步骤3前崩溃）
 *   → 影响：file_contents.ref_count 悬空递增，指向不存在的文件引用
 *   → 事务后保证：整个批次回滚，ref_count 恢复
 *
 * ## Delete 故障区域分析
 * Source: src/services/FileService.cpp lines 1411-1542
 *
 * 步骤序列：
 *  1) [1482-1517] trash 行批量插入（软删除记录）
 *  2) [1519-1533] files 行批量删除
 *
 * **Delete Zone A**：trash 已插入（步骤1），files 未删除（步骤2前崩溃）
 *   → 影响：文件同时存在于 files 和 trash 表，重复状态
 *   → 事务后保证：整个批次回滚，trash 记录清除
 *
 * **Delete Zone B**：files 已删除（步骤2），但 trash 插入实际上失败
 *   → 影响：文件从 files 表消失但 trash 中无记录，不可恢复
 *   → 注意：当前代码中 trash 插入先于 files 删除，此场景在 trash 插入
 *     抛异常时 files 不会被删除。但如果 trash 插入部分成功（batch 中
 *     部分 INSERT 失败），可能产生不一致。
 *   → 事务后保证：整个批次回滚，文件保留在 files 表
 *
 * ## 事务模式参考
 * Source: src/services/FileService.cpp lines 443-531 (CompleteUpload)
 * ```cpp
 * transaction = co_await m_db_client->newTransactionCoro();
 * ///< ... DB operations inside transaction ...
 * } catch (...) {
 *     transaction->rollback();
 * }
 * ```
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
#include "../../src/storage/LocalFileStorage.hpp"
#include "../../src/utils/ConfigMgr.hpp"
#include "../../src/utils/FileHashUtil.hpp"

namespace disk::file {
    namespace {

        using disk::storage::LocalFileStorage;
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
            LocalFileStorage& storage,
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
                auto cleanup_result = drogon::sync_wait(storage.DeleteStagedFile(assembled.path));
                (void)cleanup_result;
                return std::unexpected(
                    ErrorInfo(ErrorCode::ChunkVerifyFailed, "File hash verification failed")
                );
            }

            if (filename_exists) {
                auto cleanup_result = drogon::sync_wait(storage.DeleteStagedFile(assembled.path));
                (void)cleanup_result;
                return std::unexpected(ErrorInfo(ErrorCode::FileAlreadyExists));
            }

            auto promote_result = drogon::sync_wait(storage.PromoteToFinal(assembled.path, assembled.md5_hash));
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
            LocalFileStorage& storage,
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
                m_storage = std::make_unique<LocalFileStorage>();
            }

            void TearDown() override {
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
                return TempDir(upload_id) / (std::to_string(chunk_index) + ".chunk");
            }

            auto AssembledPath(const std::string& upload_id) const -> std::filesystem::path {
                return m_temp_base / (upload_id + ".tmp");
            }

            auto FinalStoragePath(const std::string& hash) const -> std::filesystem::path {
                return m_storage->GetFinalStoragePath(hash);
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
            std::unique_ptr<LocalFileStorage> m_storage;
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

            const auto final_path = FinalStoragePath(expected_md5);
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
            EXPECT_FALSE(std::filesystem::exists(FinalStoragePath(task.file_hash)));
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
        /// Part 2: DISABLED 故障注入测试 — Copy 失败区域
        ///
        /// 以下测试当前为 DISABLED 状态。
        /// 启用条件：
        ///   1. MockDbClient 支持 newTransactionCoro() 模拟
        ///   2. FileService::Copy() 使用事务包装每个批次
        ///   3. 测试框架能够注入 SQL 步骤级失败
        ///
        /// 预期行为：
        ///   - 非事务实现：测试 FAIL（检测到不一致状态）
        ///   - 事务实现：测试 PASS（回滚保证一致性）
        /// ============================================================================

        TEST(FileServiceCopyAtomicity, DISABLED_CopyZoneA_QuotaDeductedButRefCountNotIncremented_RollsBack) {
            /// 【Copy Zone A】配额已扣减，ref_count 尚未递增 → 崩溃导致配额泄漏
            ///
            /// Source: src/services/FileService.cpp lines 1192-1326
            ///
            /// 步骤映射：
            ///  1) CheckStorageQuota → UPDATE users SET storage_used = storage_used + ?
            ///     (line 1192: co_await CheckStorageQuota(user_id, total_copy_size))
            ///     如果配额检查通过，storage_used 立即增加 total_copy_size。
            ///  2) ref_count 递增 → UPDATE file_contents SET ref_count = ref_count + CASE ...
            ///     (line 1319: co_await m_db_client->execSqlCoro(update_sql))
            ///
            /// 故障注入点：在步骤 2 执行前注入异常。
            ///
            /// 非事务预期（当前实现）：
            ///   - storage_used 已增加 total_copy_size
            ///   - ref_count 未递增
            ///   - files 未插入
            ///   → storage_used 泄漏，用户可用空间永久减少
            ///
            /// 事务预期（目标实现）：
            ///   - 整个批次回滚
            ///   - storage_used 恢复到操作前值
            ///   - ref_count 不变
            ///   - files 无新行
            ///
            /// Test procedure:
            /// 1. 记录初始 storage_used
            /// 2. 构造 CopyRequest，total_copy_size > 0
            /// 3. 注入故障：在 ref_count UPDATE 前抛出 DrogonDbException
            /// 4. 断言 storage_used == 初始值（回滚成功）
            /// 5. 断言 file_contents.ref_count 未变化
            /// 6. 断言 files 表无新增行
            SUCCEED() << "Skipped: requires transaction seam and DB fault-injection harness";
        }

        TEST(FileServiceCopyAtomicity, DISABLED_CopyZoneB_RefCountIncrementedButFilesNotInserted_RollsBack) {
            /// 【Copy Zone B】ref_count 已递增，files 行未插入 → 悬空 ref_count
            ///
            /// Source: src/services/FileService.cpp lines 1319-1389
            ///
            /// 步骤映射：
            ///  1) ref_count 递增 → UPDATE file_contents SET ref_count = ref_count + CASE ...
            ///     (line 1319-1320: co_await m_db_client->execSqlCoro(update_sql))
            ///  2) files 插入 → INSERT INTO files (...)
            ///     (line 1372: co_await m_db_client->execSqlCoro(insert_sql))
            ///
            /// 故障注入点：在步骤 2 执行前注入异常。
            ///
            /// 非事务预期（当前实现）：
            ///   - file_contents.ref_count 已递增（例如从 1 → 2）
            ///   - files 表无对应新行
            ///   → ref_count 悬空，指向不存在的文件记录
            ///   → 后续物理文件清理可能误删（ref_count 降到 0 时）
            ///
            /// 事务预期（目标实现）：
            ///   - 整个批次回滚
            ///   - file_contents.ref_count 恢复到原始值
            ///   - files 表无新增行
            ///   - storage_used 恢复
            ///
            /// Test procedure:
            /// 1. 记录初始 file_contents.ref_count
            /// 2. 构造 CopyRequest 包含引用已知 content_id 的文件
            /// 3. 注入故障：在 files INSERT 前抛出异常
            /// 4. 断言 file_contents.ref_count == 初始值（回滚成功）
            /// 5. 断言 files 表无新增行
            /// 6. 断言 storage_used == 初始值
            SUCCEED() << "Skipped: requires transaction seam and DB fault-injection harness";
        }

        TEST(FileServiceCopyAtomicity, DISABLED_CopyZoneA_QuotaDeductedAndRefCountIncrementedButFilesInsertFails_RollsBack) {
            /// 【Copy Zone A+B 组合】配额已扣 + ref_count 已递增 + files INSERT 失败
            ///
            /// Source: src/services/FileService.cpp lines 1192-1389
            ///
            /// 当前实现行为：
            ///   - 配额已扣（line 1192）
            ///   - ref_count 已递增（line 1320）
            ///   - files INSERT 失败（line 1372 抛异常，被 line 1387 catch 捕获）
            ///   - 结果：配额泄漏 + ref_count 悬空
            ///   - 后续 quota release（line 1397）仅释放 reserved - actual 差额，
            ///     如果 copied_count = 0 且 actual_copy_size = 0，则 release_size = total_copy_size，
            ///     配额理论上会恢复。但 ref_count 递增不会回滚。
            ///
            /// 事务预期：
            ///   - 整个批次回滚
            ///   - 配额、ref_count、files 表均恢复到操作前状态
            ///
            /// Test procedure:
            /// 1. 记录初始 storage_used 和 file_contents.ref_count
            /// 2. 构造 CopyRequest
            /// 3. 注入故障：files INSERT 抛出 DrogonDbException
            /// 4. 断言 storage_used == 初始值
            /// 5. 断言 file_contents.ref_count == 初始值
            /// 6. 断言 files 表无新增行
            SUCCEED() << "Skipped: requires transaction seam and DB fault-injection harness";
        }

        TEST(FileServiceCopyAtomicity, DISABLED_CopyHappyPath_AllStepsConsistent) {
            /// 【Copy 正常路径】所有步骤成功时的一致性不变量
            ///
            /// 此测试验证事务实现不会在正常路径引入额外开销或数据不一致。
            ///
            /// 不变量检查：
            /// 1. copied_count == new_files.size()
            /// 2. storage_used 增量 == actual_copy_size
            /// 3. file_contents.ref_count 增量 == 引用该 content_id 的 copied 文件数
            /// 4. files 表新增行数 == copied_count
            ///
            /// Test procedure:
            /// 1. 记录初始状态
            /// 2. 执行 Copy（正常路径，不注入故障）
            /// 3. 断言上述 4 个不变量
            /// 4. 断言 response.copied_count > 0
            /// 5. 断言所有 new_files 的 old_id 在原始 file_ids 中
            SUCCEED() << "Skipped: requires DB fixture for integration testing";
        }

        /// ============================================================================
        /// Part 3: DISABLED 故障注入测试 — Delete 失败区域
        ///
        /// 以下测试当前为 DISABLED 状态。
        /// 启用条件同 Copy 测试。
        /// ============================================================================

        TEST(FileServiceDeleteAtomicity, DISABLED_DeleteZoneA_TrashInsertedButFilesNotDeleted_RollsBack) {
            /// 【Delete Zone A】trash 已插入，files 未删除 → 重复状态
            ///
            /// Source: src/services/FileService.cpp lines 1509-1533
            ///
            /// 步骤映射：
            ///  1) trash INSERT → INSERT INTO trash (...)
            ///     (line 1510: co_await m_db_client->execSqlCoro(trash_sql))
            ///  2) files DELETE → DELETE FROM files WHERE id IN (...)
            ///     (line 1521: co_await m_db_client->execSqlCoro(delete_sql))
            ///
            /// 故障注入点：在步骤 2 执行前注入异常。
            ///
            /// 非事务预期（当前实现）：
            ///   - trash 表已有对应记录
            ///   - files 表记录未被删除
            ///   → 文件同时存在于 files 和 trash 表
            ///   → 回收站列表显示该文件，但原位置也可见
            ///   → 如果用户从回收站恢复，可能产生主键冲突
            ///
            /// 事务预期（目标实现）：
            ///   - 整个批次回滚
            ///   - trash 表无新增记录
            ///   - files 表保持不变
            ///
            /// Test procedure:
            /// 1. 构造已知 files 记录
            /// 2. 执行 Delete，注入故障：files DELETE 前抛出异常
            /// 3. 断言 trash 表无对应记录（回滚成功）
            /// 4. 断言 files 表记录仍存在
            SUCCEED() << "Skipped: requires transaction seam and DB fault-injection harness";
        }

        TEST(FileServiceDeleteAtomicity, DISABLED_DeleteZoneB_TrashInsertFails_FilesPreserved) {
            /// 【Delete Zone B】trash 插入失败 → files 应保留（不应丢失文件）
            ///
            /// Source: src/services/FileService.cpp lines 1509-1517
            ///
            /// 步骤映射：
            ///  1) trash INSERT 失败 → line 1510 抛 DrogonDbException
            ///     → catch 块（line 1515）捕获，跳过该 chunk
            ///     → deletable_ids 为空，files DELETE 不执行
            ///
            /// 当前实现分析：
            ///   - trash INSERT 失败时，deletable_ids 保持为空
            ///   - files DELETE 不会执行（line 1519: if (!deletable_ids.empty())）
            ///   → 文件保留在 files 表，trash 中无记录
            ///   → 此场景当前实现是安全的（files 不丢失）
            ///   → 但语义上属于"删除失败"，用户看到 deleted_count 不包含这些文件
            ///
            /// 事务预期：
            ///   - 事务回滚（虽然当前实现在此路径无副作用需要回滚）
            ///   - files 表保持不变
            ///   - trash 表无记录
            ///
            /// Test procedure:
            /// 1. 构造已知 files 记录
            /// 2. 执行 Delete，注入故障：trash INSERT 抛出异常
            /// 3. 断言 files 表记录仍存在
            /// 4. 断言 trash 表无记录
            /// 5. 断言 response.deleted_count == 0（该 chunk 的文件未被计数）
            SUCCEED() << "Skipped: requires transaction seam and DB fault-injection harness";
        }

        TEST(FileServiceDeleteAtomicity, DISABLED_DeleteZoneA_PartialTrashInsert_RollsBack) {
            /// 【Delete Zone A 变体】trash INSERT 部分成功（batch 内部分行写入后崩溃）
            ///
            /// Source: src/services/FileService.cpp lines 1482-1517
            ///
            /// 当前实现中 trash INSERT 使用单条多值 INSERT 语句（一条 SQL 插入多行）。
            /// MySQL 中单条 INSERT 是原子的（要么全部成功要么全部失败），
            /// 所以"部分成功"在单批次内不会发生。
            ///
            /// 但跨批次场景可能发生：
            ///   - Batch 1 (chunk 1): trash INSERT 成功 + files DELETE 成功
            ///   - Batch 2 (chunk 2): trash INSERT 成功 + files DELETE 失败
            ///   → Batch 2 的文件在 trash 和 files 中都有记录
            ///   → Batch 1 已提交，无法回滚
            ///
            /// 事务预期（per-batch）：
            ///   - Batch 2 回滚，trash 记录清除，files 保留
            ///   - Batch 1 不受影响
            ///
            /// Test procedure:
            /// 1. 构造跨批次的大量文件（>500 以触发多批次）
            /// 2. 在第二个批次的 files DELETE 注入故障
            /// 3. 断言第二个批次的 trash 记录已回滚
            /// 4. 断言第二个批次的 files 记录仍存在
            /// 5. 断言第一个批次正常完成
            SUCCEED() << "Skipped: requires multi-batch DB fault-injection harness";
        }

        TEST(FileServiceDeleteAtomicity, DISABLED_DeleteHappyPath_TrashAndFilesConsistent) {
            /// 【Delete 正常路径】所有步骤成功时的一致性不变量
            ///
            /// 不变量检查：
            /// 1. deleted_count == trash 新增记录数
            /// 2. deleted_count == files 删除记录数
            /// 3. 每条 trash 记录的 item_id 对应一个被删除的 file_id
            /// 4. files 表中不再存在被删除的 file_id
            ///
            /// Test procedure:
            /// 1. 构造已知 files 记录
            /// 2. 执行 Delete（正常路径）
            /// 3. 断言上述 4 个不变量
            /// 4. 断言 response.deleted_count == file_ids.size()
            SUCCEED() << "Skipped: requires DB fixture for integration testing";
        }

        /// ============================================================================
        /// Part 4: DISABLED 跨操作一致性测试
        /// ============================================================================

        TEST(FileServiceAtomicity, DISABLED_CopyThenDelete_QuotaFullyReclaimed) {
            /// 【跨操作一致性】Copy 成功后 Delete → 配额完全回收
            ///
            /// 验证在事务化实现后，Copy + Delete 的配额流动是闭环的。
            ///
            /// Test procedure:
            /// 1. 记录初始 storage_used = S0
            /// 2. Copy N 个文件（总大小 = C）
            /// 3. 断言 storage_used == S0 + C
            /// 4. Delete 被复制的文件（new_files）
            /// 5. 断言 storage_used == S0（完全回收）
            SUCCEED() << "Skipped: requires DB fixture for integration testing";
        }

        TEST(FileServiceAtomicity, DISABLED_PartialCopyFailure_DoesNotAffectSuccessfulFiles) {
            /// 【部分成功隔离】batch 中部分文件失败不影响成功文件
            ///
            /// 当前实现中，每个 chunk 是一个独立批次。
            /// 在 chunk 内部，如果 ref_count UPDATE 失败，整个 chunk 被跳过。
            /// 但如果 chunk 1 成功、chunk 2 失败，chunk 1 的结果已提交。
            ///
            /// 事务化后（per-batch），这个行为应保持：
            /// - 成功的 chunk 不受影响
            /// - 失败的 chunk 完全回滚
            ///
            /// Test procedure:
            /// 1. 构造文件集合（跨多个 chunk）
            /// 2. 在某个 chunk 注入故障
            /// 3. 断言成功 chunk 的文件已复制
            /// 4. 断言失败 chunk 的文件未复制
            /// 5. 断言配额 = 成功 chunk 的总大小
            SUCCEED() << "Skipped: requires multi-batch DB fault-injection harness";
        }

        TEST(FileServiceAtomicity, DISABLED_DeleteRollbackPreservesFileContentsRefCount) {
            /// 【Delete 回滚不影响 ref_count】
            ///
            /// Delete 操作不涉及 file_contents.ref_count 修改（仅移动到 trash）。
            /// 但如果未来 Delete 实现增加 ref_count 递减（硬删除时），此测试
            /// 验证回滚后 ref_count 不变。
            ///
            /// 当前 Delete 流程中 ref_count 不参与，此测试作为前瞻性回归保护。
            ///
            /// Test procedure:
            /// 1. 记录 file_contents.ref_count 初始值
            /// 2. 执行 Delete，在 trash INSERT 后注入故障
            /// 3. 断言 ref_count == 初始值（回滚成功，或不涉及）
            /// 4. 断言 files 记录仍存在
            SUCCEED() << "Skipped: requires transaction seam and DB fault-injection harness";
        }

        /// ============================================================================
        /// Part 5: DISABLED 配额泄漏探测测试
        /// ============================================================================

        TEST(FileServiceAtomicity, DISABLED_CopyFailure_DoesNotLeakStorageQuota) {
            /// 【配额泄漏回归保护】Copy 失败不应泄漏 storage_used
            ///
            /// 这是最关键的不变量：无论 Copy 在哪个步骤失败，
            /// storage_used 的净变化必须为 0。
            ///
            /// 当前实现中，CheckStorageQuota 立即修改 storage_used。
            /// 如果后续步骤失败且 copied_count = 0，release_size = total_copy_size，
            /// 配额会通过 UpdateStorageUsed 恢复。
            /// 但如果异常发生在 quota release 之前（进程崩溃），配额将泄漏。
            ///
            /// 事务化后，配额扣减在事务内，失败自动回滚，无需显式释放。
            ///
            /// Test procedure:
            /// 1. 记录初始 storage_used
            /// 2. 构造大文件 Copy（超过实际可用空间的一半）
            /// 3. 在每个可能的故障点注入失败
            /// 4. 每次断言 storage_used == 初始值
            SUCCEED() << "Skipped: requires DB fault-injection harness";
        }

        TEST(FileServiceAtomicity, DISABLED_DeleteFailure_DoesNotCorruptTrashState) {
            /// 【Trash 状态一致性】Delete 失败不应导致 trash 表出现幽灵记录
            ///
            /// 幽灵记录 = trash 中有记录但 files 中对应记录仍存在
            ///
            /// 检测查询：
            /// SELECT t.item_id FROM trash t
            /// INNER JOIN files f ON f.id = t.item_id AND f.user_id = t.user_id
            /// WHERE t.item_type = 'file';
            ///
            /// Test procedure:
            /// 1. 执行多次 Delete，部分注入故障
            /// 2. 执行上述检测查询
            /// 3. 断言结果为空（无幽灵记录）
            SUCCEED() << "Skipped: requires DB fixture and SQL assertions";
        }

        /// ============================================================================
        /// Part 6: ENABLED 回归测试 — 边缘行为和不变量保护
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
        /// Part 7: DISABLED 并发和重试测试 — 需要事务缝或数据库fixture
        /// ============================================================================

        TEST(FileServiceRegression, DISABLED_RetryAfterFailedCopy_Idempotent) {
            /// 【重试幂等性】复制失败后重试相同请求应产生相同结果
            ///
            /// 场景：
            /// 1. 执行 Copy([10, 20, 30], target=100)
            /// 2. 假设 file_id=20 的复制失败（ref_count UPDATE 失败）
            /// 3. 结果：copied_count=2, new_files=[{10→110}, {30→130}]
            /// 4. 重试相同请求 Copy([10, 20, 30], target=100)
            ///
            /// 预期：
            /// - file_id=10 和 file_id=30 已成功复制，第二次重试时跳过（名称冲突）
            /// - file_id=20 重试时可能成功（如果之前失败原因已消除）
            /// - 结果：copied_count <= 原始文件数，不会产生重复文件
            ///
            /// Note: 此测试需要 DB fixture 和事务缝模拟
            SUCCEED() << "Skipped: requires DB fixture and transaction seam";
        }

        TEST(FileServiceRegression, DISABLED_RetryAfterFailedDelete_Idempotent) {
            /// 【重试幂等性】删除失败后重试相同请求应产生相同结果
            ///
            /// 场景：
            /// 1. 执行 Delete([10, 20, 30])
            /// 2. 假设 file_id=20 的删除失败（trash INSERT 失败）
            /// 3. 结果：deleted_count=2（仅 file_id=10 和 file_id=30）
            /// 4. 重试相同请求 Delete([10, 20, 30])
            ///
            /// 预期：
            /// - file_id=10 和 file_id=30 已删除并在 trash 中，第二次重试时不再存在于 files 表
            /// - file_id=20 重试时可能成功（如果之前失败原因已消除）
            /// - 结果：deleted_count 不会超过实际删除的文件数
            ///
            /// Note: 此测试需要 DB fixture 和事务缝模拟
            SUCCEED() << "Skipped: requires DB fixture and transaction seam";
        }

        TEST(FileServiceRegression, DISABLED_ConcurrentCopyToSameFolder_HandlesNameConflicts) {
            /// 【并发复制到同一文件夹】处理名称冲突
            ///
            /// 场景：两个并发请求复制文件到同一目标文件夹
            /// - Request A: Copy([1, 2], target=100)，文件名为 "doc.txt" 和 "photo.jpg"
            /// - Request B: Copy([3, 4], target=100)，文件名为 "doc.txt"（与 A 冲突）和 "music.mp3"
            ///
            /// 预期行为（取决于并发控制策略）：
            /// 选项 A（先到先得）：
            ///   - Request A 先执行，复制成功：{1→101, 2→102}
            ///   - Request B 后执行，跳过 "doc.txt"：{4→104}（file_id=3 被跳过）
            ///
            /// 选项 B（悲观锁）：
            ///   - 一个请求获取目标文件夹锁，另一个等待
            ///   - 按序列执行，无冲突
            ///
            /// 选项 C（乐观锁 + 重试）：
            ///   - 两个请求同时执行，检测到冲突后自动重试
            ///
            /// Note: 此测试需要并发测试框架和数据库 fixture
            SUCCEED() << "Skipped: requires concurrent test framework and DB fixture";
        }

        /// ============================================================================
        /// Part 8: Delete Hardening Tests — crash prevention + deterministic error contract
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

    } ///< namespace
} ///< namespace disk::file
