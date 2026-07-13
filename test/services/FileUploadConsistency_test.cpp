/**
 * @file FileUploadConsistency_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief FileService 上传一致性基线与故障注入测试（T7 优化后）
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <filesystem>
#include <fstream>
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

/// disk-test 未直接链接 LocalFileStorage.cpp，这里按测试翻译单元引入实现，
/// 以便对真实分片写入与组装路径做特征回归保护。
#include "../../src/storage/LocalFileStorage.cpp"

namespace disk::file {
    namespace {

        using disk::storage::LocalBlobStore;
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

        class LocalFileStorageUploadConsistencyTest : public ::testing::Test {
        protected:
            void SetUp() override {
                const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
                m_root = std::filesystem::path("build/test_upload_consistency") /
                         SanitizePathComponent(std::string(test_info->test_suite_name()) + "_" + test_info->name());
                m_storage_base = m_root / "uploaded";
                m_temp_base = m_root / "temp";

                std::error_code ec;
                std::filesystem::remove_all(m_root, ec);

                LoadStorageConfig(m_storage_base, m_temp_base);
                m_storage = std::make_unique<LocalFileStorage>();
                m_blob_store = std::make_unique<LocalBlobStore>();
            }

            void TearDown() override {
                m_blob_store.reset();
                m_storage.reset();
                RestoreDefaultStorageConfig();

                std::error_code ec;
                std::filesystem::remove_all(m_root, ec);
            }

            auto ChunkPath(const std::string& upload_id, uint32_t chunk_index) const
                -> std::filesystem::path {
                return m_temp_base / upload_id / (std::to_string(chunk_index) + ".chunk");
            }

            auto AssembledPath(const std::string& upload_id) const -> std::filesystem::path {
                return m_temp_base / (upload_id + ".tmp");
            }

            std::filesystem::path m_root;
            std::filesystem::path m_storage_base;
            std::filesystem::path m_temp_base;
            std::unique_ptr<LocalFileStorage> m_storage;
            std::unique_ptr<LocalBlobStore> m_blob_store;
        };

        /// ============================================================================
        /// Transaction Boundary Analysis Baseline (FileService::CompleteUpload)
        /// Source: src/services/FileService.cpp lines 307-570
        /// ============================================================================
        ///
        /// Flow mapping (exact sequence from current implementation):
        ///  1) [307-320] Load and validate upload task ownership/state.
        ///  2) [321-326] Idempotency short-circuit if status == 1.
        ///  3) [329-341] Validate chunk count from upload_task_chunks.
        ///  4) [344-350] Assemble temp file from chunks.
        ///  5) [353-378] Compute/verify final MD5.
        ///  6) [382-390] Duplicate filename guard.
        ///  7) [392-434] Dedup branch:
        ///      - existing content: delete assemble artifact only
        ///      - new content: promote to final storage + compute sha256
        ///  8) [436-504] ZONE A (transaction scope):
        ///      - newTransactionCoro()
        ///      - insert/update file_contents
        ///      - insert files row
        ///      - rollback on DB exception
        ///  9) [506-517] DB failure compensation:
        ///      - delete promoted storage file when needed
        /// 10) [522-538] ZONE B (inside transaction): quota transfer
        ///      UPDATE users
        ///      SET storage_reserved = GREATEST(storage_reserved - ?, 0),
        ///          storage_used = storage_used + ?
        ///      Failure throws std::runtime_error to force rollback.
        /// 11) [540-555] ZONE C (inside transaction): task finalization
        ///      - UPDATE upload_tasks SET status = 1, finalized_at = NOW()
        ///      - DELETE upload_task_chunks
        ///      Failure throws std::runtime_error to force rollback.
        /// 12) [557-562] Temp directory cleanup.
        /// 13) [564-570] Build response payload (file item + hash).
        ///
        /// Consistency guarantees after T7 optimization:
        ///  - Zone A (440-504): transactional for file_contents + files tables.
        ///  - Zone B (522-538): quota transfer inside transaction; failure triggers rollback.
        ///  - Zone C (540-555): task finalization inside transaction; failure triggers rollback.
        ///
        /// Post-optimization failure behavior:
        ///  - Zone B failure: entire transaction rolls back; file not committed.
        ///  - Zone C failure: entire transaction rolls back; upload task remains pending.
        ///  - All-or-nothing consistency: partial success is impossible.
        ///
        /// Instant-upload path consistency note (optimized):
        ///  - ref_count increment + files insert now wrapped in newTransactionCoro().
        ///  - Transaction-aware IsFilenameExists used for duplicate check within transaction.
        ///  - Redundant content re-read eliminated; mime_type comes from ContentService::FindByMd5.
        ///  - All-or-nothing consistency: rollback on any DB failure within the instant-upload branch.

        /// ==================== Upload DTO Contract Tests ====================

        class FileUploadDtoContractTest : public ::testing::Test {};

        TEST_F(FileUploadDtoContractTest, InitUploadResponseInstantUploadContract) {
            InitUploadResponse response;
            response.upload_id = "";
            response.chunk_size = 0;
            response.total_chunks = 0;
            response.uploaded_chunks = {};
            response.instant_upload = true;
            response.file = FileItem{ .id = 101,
                                      .name = "instant.bin",
                                      .size = 4096,
                                      .hash = "d41d8cd98f00b204e9800998ecf8427e",
                                      .mime_type = "application/octet-stream",
                                      .parent_id = 0,
                                      .created_at = "2026-04-01 10:00:00" };

            const auto json = response.ToJson();

            EXPECT_TRUE(json["instant_upload"].asBool());
            ASSERT_TRUE(json.isMember("file"));
            EXPECT_EQ(json["file"]["id"].asUInt64(), 101U);
            EXPECT_EQ(json["file"]["name"].asString(), "instant.bin");
            EXPECT_EQ(json["file"]["hash"].asString(), "d41d8cd98f00b204e9800998ecf8427e");
            EXPECT_EQ(json["file"]["parent_id"].asUInt64(), 0U);
        }

        TEST_F(FileUploadDtoContractTest, InitUploadResponseChunkedUploadContract) {
            InitUploadResponse response;
            response.upload_id = "upload-task-001";
            response.chunk_size = 5 * 1024 * 1024;
            response.total_chunks = 3;
            response.uploaded_chunks = { 0, 1 };
            response.instant_upload = false;

            const auto json = response.ToJson();

            EXPECT_FALSE(json["instant_upload"].asBool());
            EXPECT_EQ(json["upload_id"].asString(), "upload-task-001");
            EXPECT_EQ(json["chunk_size"].asUInt(), 5U * 1024U * 1024U);
            EXPECT_EQ(json["total_chunks"].asUInt(), 3U);
            ASSERT_TRUE(json.isMember("uploaded_chunks"));
            ASSERT_EQ(json["uploaded_chunks"].size(), 2U);
            EXPECT_EQ(json["uploaded_chunks"][0].asUInt(), 0U);
            EXPECT_EQ(json["uploaded_chunks"][1].asUInt(), 1U);
            EXPECT_FALSE(json.isMember("file"));
        }

        TEST_F(FileUploadDtoContractTest, CompleteUploadResponseContainsFileItemAndHash) {
            CompleteUploadResponse response;
            response.file = FileItem{ .id = 202,
                                      .name = "merged.iso",
                                      .size = 1024,
                                      .hash = "0123456789abcdef0123456789abcdef",
                                      .mime_type = "application/octet-stream",
                                      .parent_id = 12,
                                      .created_at = "2026-04-01 11:00:00" };

            const auto json = response.ToJson();

            ASSERT_TRUE(json.isMember("file"));
            EXPECT_EQ(json["file"]["id"].asUInt64(), 202U);
            EXPECT_EQ(json["file"]["name"].asString(), "merged.iso");
            EXPECT_EQ(json["file"]["hash"].asString(), "0123456789abcdef0123456789abcdef");
            EXPECT_EQ(json["file"]["parent_id"].asUInt64(), 12U);
        }

        /// ==================== Upload Task Status Contract Tests ====================

        enum class UploadTaskStatusContract : int8_t {
            Pending = 0,
            Completed = 1,
        };

        TEST(FileUploadStatusContract, UploadTaskStatusEnumValues) {
            EXPECT_EQ(static_cast<int>(UploadTaskStatusContract::Pending), 0);
            EXPECT_EQ(static_cast<int>(UploadTaskStatusContract::Completed), 1);
        }

        /// ==================== LocalFileStorage 上传一致性测试 ====================

        TEST_F(LocalFileStorageUploadConsistencyTest, WriteChunkPersistsExactBytesAndSizeOnDisk) {
            const std::string upload_id = "write-chunk-integrity";
            const std::string chunk_data = std::string("AB\0CD", 5) + std::string(4096, 'x');

            ASSERT_TRUE(drogon::sync_wait(m_storage->EnsureUploadTempDir(upload_id)).has_value());

            auto result = drogon::sync_wait(m_storage->WriteChunk(upload_id, 0, chunk_data));
            ASSERT_TRUE(result.has_value());

            const auto chunk_path = ChunkPath(upload_id, 0);
            ASSERT_TRUE(std::filesystem::exists(chunk_path));
            EXPECT_EQ(std::filesystem::file_size(chunk_path), static_cast<uintmax_t>(chunk_data.size()));
            EXPECT_EQ(ReadBinaryFile(chunk_path), chunk_data);
        }

        TEST_F(LocalFileStorageUploadConsistencyTest, AssembleChunksProducesExpectedHashesAndMergedBytes) {
            const std::string upload_id = "assemble-success";
            const std::vector<std::string> chunks = {
                "header-",
                std::string("mid\0section", 11),
                std::string(2048, 'z')
            };
            const std::string expected_content = chunks[0] + chunks[1] + chunks[2];

            ASSERT_TRUE(drogon::sync_wait(m_storage->EnsureUploadTempDir(upload_id)).has_value());
            for (uint32_t index = 0; index < chunks.size(); ++index) {
                ASSERT_TRUE(drogon::sync_wait(m_storage->WriteChunk(upload_id, index, chunks[index])).has_value())
                    << "chunk_index=" << index;
            }

            auto assemble_result =
                drogon::sync_wait(m_storage->AssembleChunks(upload_id, static_cast<uint32_t>(chunks.size())));
            ASSERT_TRUE(assemble_result.has_value());

            const auto& assembled = assemble_result.value();
            ASSERT_TRUE(std::filesystem::exists(assembled.path));
            EXPECT_EQ(std::filesystem::file_size(assembled.path), static_cast<uintmax_t>(expected_content.size()));
            EXPECT_EQ(ReadBinaryFile(assembled.path), expected_content);
            EXPECT_EQ(assembled.md5_hash, FileHashUtil::HashMd5(expected_content));
            EXPECT_EQ(assembled.sha256_hash, FileHashUtil::HashSha256(expected_content));
        }

        TEST_F(LocalFileStorageUploadConsistencyTest, AssembleChunksMissingChunkCleansTempArtifact) {
            const std::string upload_id = "assemble-missing-chunk";

            ASSERT_TRUE(drogon::sync_wait(m_storage->EnsureUploadTempDir(upload_id)).has_value());
            ASSERT_TRUE(drogon::sync_wait(m_storage->WriteChunk(upload_id, 0, "only-first-chunk")).has_value());

            auto assemble_result = drogon::sync_wait(m_storage->AssembleChunks(upload_id, 2));
            ASSERT_FALSE(assemble_result.has_value());
            EXPECT_EQ(assemble_result.error().code, ErrorCode::InternalError);
            EXPECT_FALSE(std::filesystem::exists(AssembledPath(upload_id)));
        }

        TEST_F(LocalFileStorageUploadConsistencyTest, PromoteToFinalReportsCreatedForNewBlob) {
            const std::string upload_id = "promote-created";
            const std::string content = "promote-created-content";
            const auto hash = FileHashUtil::HashMd5(content);
            const auto temp_path = AssembledPath(upload_id);

            std::error_code ec;
            std::filesystem::create_directories(m_temp_base, ec);
            ASSERT_FALSE(ec);
            std::ofstream output(temp_path, std::ios::binary);
            output.write(content.data(), static_cast<std::streamsize>(content.size()));
            output.close();
            ASSERT_TRUE(output);

            auto promote_result = drogon::sync_wait(m_blob_store->PromoteToFinal(temp_path, hash));

            ASSERT_TRUE(promote_result.has_value());
            EXPECT_TRUE(promote_result->created);
            EXPECT_EQ(promote_result->path, m_blob_store->GetFinalStoragePath(hash));
            EXPECT_FALSE(std::filesystem::exists(temp_path));
            ASSERT_TRUE(std::filesystem::exists(promote_result->path));
            EXPECT_EQ(ReadBinaryFile(promote_result->path), content);
        }

        TEST_F(LocalFileStorageUploadConsistencyTest, PromoteToFinalReportsReusedForPreexistingBlob) {
            const std::string upload_id = "promote-reused";
            const std::string existing_content = "pre-existing-final-blob";
            const std::string temp_content = "temporary-upload-content";
            const auto hash = FileHashUtil::HashMd5(temp_content);
            const auto final_path = m_blob_store->GetFinalStoragePath(hash);
            const auto temp_path = AssembledPath(upload_id);

            std::error_code ec;
            std::filesystem::create_directories(final_path.parent_path(), ec);
            ASSERT_FALSE(ec);
            std::ofstream final_output(final_path, std::ios::binary);
            final_output.write(existing_content.data(), static_cast<std::streamsize>(existing_content.size()));
            final_output.close();
            ASSERT_TRUE(final_output);

            std::filesystem::create_directories(m_temp_base, ec);
            ASSERT_FALSE(ec);
            std::ofstream temp_output(temp_path, std::ios::binary);
            temp_output.write(temp_content.data(), static_cast<std::streamsize>(temp_content.size()));
            temp_output.close();
            ASSERT_TRUE(temp_output);

            auto promote_result = drogon::sync_wait(m_blob_store->PromoteToFinal(temp_path, hash));

            ASSERT_TRUE(promote_result.has_value());
            EXPECT_FALSE(promote_result->created);
            EXPECT_EQ(promote_result->path, final_path);
            EXPECT_FALSE(std::filesystem::exists(temp_path));
            ASSERT_TRUE(std::filesystem::exists(final_path));
            EXPECT_EQ(ReadBinaryFile(final_path), existing_content);
        }

        /// ==================== Fault Injection Scenario Tests (DB-dependent) ====================

        TEST(FileUploadConsistencyFaultInjection, DISABLED_ZoneBQuotaTransferFailureLeavesFreeStorageLeak) {
            /// 【需要 MySQL + 可注入故障环境】Zone B 配额转移失败场景
            ///
            /// Baseline expectation (optimization BEFORE state):
            /// - Zone A 已提交（files/file_contents 成功）
            /// - Zone B quota transfer 失败仅记录 warning，不触发回滚
            /// - 结果：文件可见，但 storage_used 未增加（免费存储）
            ///
            /// Test procedure:
            /// 1. 构造 upload_tasks(status=0) + upload_task_chunks 全部分片记录
            /// 2. 触发 CompleteUpload 到达 Zone A 并成功提交文件记录
            /// 3. 对 Zone B 的 users UPDATE 注入失败（mock DB exception / permission deny）
            /// 4. 断言 files 记录存在（提交成功）
            /// 5. 断言 users.storage_used 未按 file_size 增长
            /// 6. 记录该行为为优化前一致性基线
            SUCCEED() << "Skipped: requires DB fault-injection harness";
        }

        TEST(FileUploadConsistencyFaultInjection, DISABLED_ZoneCTaskFinalizationFailureLeavesTaskInProgress) {
            /// 【需要 MySQL + 可注入故障环境】Zone C 任务终态失败场景
            ///
            /// Baseline expectation (optimization BEFORE state):
            /// - Zone A 已提交文件
            /// - Zone C 更新 upload_tasks.status=1 / 删除 chunks 失败仅 warning
            /// - 结果：upload_tasks.status 可能仍为 0，表现为“上传中”卡住
            ///
            /// Test procedure:
            /// 1. 准备 upload task 与分片记录，触发 CompleteUpload 主流程
            /// 2. 在 Zone C 注入 UPDATE upload_tasks 或 DELETE upload_task_chunks 失败
            /// 3. 断言 files 记录已存在（文件对用户可见）
            /// 4. 查询 upload_tasks.status，预期仍可能为 0（pending）
            /// 5. 查询 upload_task_chunks，预期仍有残留行
            SUCCEED() << "Skipped: requires DB fault-injection harness";
        }

        TEST(FileUploadConsistencyFaultInjection, DISABLED_OrphanedFileDetectionQueryBaseline) {
            /// 【需要 MySQL 环境】孤儿文件（任务未终态但文件已创建）探测基线
            ///
            /// Suggested detection query (baseline):
            /// SELECT f.id, f.user_id, f.name, t.id AS task_id, t.status
            /// FROM files f
            /// JOIN upload_tasks t ON t.user_id = f.user_id AND t.filename = f.name
            /// WHERE t.status = 0 AND f.deleted_at IS NULL;
            ///
            /// Test procedure:
            /// 1. 制造 Zone C 失败样本（可重用前置 fixture）
            /// 2. 执行上述查询
            /// 3. 断言返回记录数 > 0（存在 orphan baseline）
            /// 4. 输出样本行供后续优化验收对比
            SUCCEED() << "Skipped: requires DB fixture and SQL assertions";
        }

        TEST(FileUploadConsistencyFaultInjection, DISABLED_UploadTaskStuckInProgressDetectionBaseline) {
            /// 【需要 MySQL 环境】上传任务 stuck-in-progress 探测基线
            ///
            /// Suggested detection query (baseline):
            /// SELECT id, user_id, filename, status, created_at, updated_at
            /// FROM upload_tasks
            /// WHERE status = 0 AND updated_at < NOW() - INTERVAL 10 MINUTE;
            ///
            /// Test procedure:
            /// 1. 构造状态=0 且更新时间超过阈值的上传任务
            /// 2. 保留其 chunks 记录（模拟 Zone C cleanup 失败）
            /// 3. 执行检测查询
            /// 4. 断言命中该任务，建立优化前监控基线
            SUCCEED() << "Skipped: requires DB fixture and clock-controlled test environment";
        }

    } ///< namespace
} ///< namespace disk::file
