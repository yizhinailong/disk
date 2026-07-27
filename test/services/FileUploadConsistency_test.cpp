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
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <drogon/drogon.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>
#include <json/json.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "../../src/dtos/FileDto.hpp"
#include "../../src/storage/LocalBlobStore.hpp"
#include "../../src/storage/LocalFileStorage.hpp"
#include "../../src/utils/ConfigMgr.hpp"
#include "../../src/utils/FileHashUtil.hpp"
#include "../../src/utils/LogHelper.hpp"
#include "../storage/UploadStagingTestAdapter.hpp"

namespace disk::file {
    namespace {

        using disk::storage::LocalBlobStore;
        using disk::storage::LocalFileStorage;
        using disk::test_support::UploadStagingTestAdapter;
        using disk::utils::ConfigMgr;
        using disk::utils::FileHashUtil;
        using disk::utils::LogContext;
        using disk::utils::Logger;

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

            auto ChunkPath(const std::string& upload_id, uint32_t chunk_index) const
                -> std::filesystem::path {
                return m_storage->ChunkPath(m_temp_base, upload_id, chunk_index);
            }

            auto AssembledPath(const std::string& upload_id) const -> std::filesystem::path {
                return m_temp_base / (upload_id + ".tmp");
            }

            auto FinalStoragePath(const std::string& hash) const -> std::filesystem::path {
                return m_storage_base / "sha256" / hash.substr(0, 2) / (hash + ".bin");
            }

            std::filesystem::path m_root;
            std::filesystem::path m_storage_base;
            std::filesystem::path m_temp_base;
            std::unique_ptr<UploadStagingTestAdapter> m_storage;
            std::unique_ptr<LocalBlobStore> m_blob_store;
        };

        class LocalFileStorageAssemblyLogTest : public LocalFileStorageUploadConsistencyTest {
        protected:
            void SetUp() override {
                LocalFileStorageUploadConsistencyTest::SetUp();
                m_previous_logger = spdlog::default_logger();
                m_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(m_output);
                m_logger = std::make_shared<spdlog::logger>("local-assembly-log-test", m_sink);
                Logger::ApplyStructuredFormatter(m_logger);
                m_logger->set_level(spdlog::level::debug);
                spdlog::set_default_logger(m_logger);
                Logger::SetInstanceId("local-assembly-test-instance");
            }

            void TearDown() override {
                Logger::SetInstanceId("");
                spdlog::set_default_logger(m_previous_logger);
                m_logger.reset();
                m_sink.reset();
                LocalFileStorageUploadConsistencyTest::TearDown();
            }

            [[nodiscard]]
            auto AssemblyRecords() -> std::vector<Json::Value> {
                m_logger->flush();
                std::vector<Json::Value> records;
                std::istringstream lines(m_output.str());
                std::string line;
                while (std::getline(lines, line)) {
                    if (line.empty()) {
                        continue;
                    }

                    Json::CharReaderBuilder builder;
                    builder["collectComments"] = false;
                    const auto reader = std::unique_ptr<Json::CharReader>(
                        builder.newCharReader()
                    );
                    Json::Value record;
                    std::string errors;
                    if (!reader->parse(
                            line.data(),
                            line.data() + line.size(),
                            &record,
                            &errors
                        )) {
                        ADD_FAILURE() << "Invalid structured log line: " << errors;
                        continue;
                    }

                    const auto message = record["message"].asString();
                    if (std::string_view(message).starts_with("[assemble_chunks]") ||
                        std::string_view(message).starts_with("Assembly ")) {
                        records.push_back(std::move(record));
                    }
                }
                return records;
            }

            [[nodiscard]]
            auto AssembleWithContext(
                const std::string& upload_id,
                uint64_t state_version,
                LogContext log_context
            ) -> Result<disk::storage::UploadStagingAssembly> {
                const disk::storage::UploadStagingSession session{
                    .upload_id = upload_id,
                    .backend = disk::storage::UploadStagingBackend::Local,
                    .prefix = upload_id,
                };
                const auto chunks = m_storage->DescriptorsFor(upload_id);
                return drogon::sync_wait(
                    static_cast<LocalFileStorage&>(*m_storage).AssembleChunks(session, state_version, static_cast<uint32_t>(chunks.size()), chunks, std::move(log_context))
                );
            }

            std::shared_ptr<spdlog::logger> m_previous_logger;
            std::ostringstream m_output;
            std::shared_ptr<spdlog::sinks::ostream_sink_mt> m_sink;
            std::shared_ptr<spdlog::logger> m_logger;
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

        TEST_F(LocalFileStorageUploadConsistencyTest, HeadChunkObjectReportsPresentAndMissingExactDescriptor) {
            const std::string upload_id = "head-chunk-object";
            const std::string chunk_data = "diagnostic-payload";
            const disk::storage::UploadStagingSession session{
                .upload_id = upload_id,
                .backend = disk::storage::UploadStagingBackend::Local,
                .prefix = upload_id,
            };

            ASSERT_TRUE(drogon::sync_wait(m_storage->EnsureUploadTempDir(upload_id)).has_value());
            auto written = drogon::sync_wait(m_storage->WriteChunk(upload_id, 0, chunk_data));
            ASSERT_TRUE(written.has_value());

            auto present = drogon::sync_wait(m_storage->HeadChunkObject(session, written.value()));
            ASSERT_TRUE(present.has_value());
            EXPECT_TRUE(present->exists);
            EXPECT_EQ(present->size_bytes, chunk_data.size());
            EXPECT_FALSE(present->etag.has_value());

            std::error_code error;
            ASSERT_TRUE(std::filesystem::remove(ChunkPath(upload_id, 0), error));
            ASSERT_FALSE(error);
            auto missing = drogon::sync_wait(m_storage->HeadChunkObject(session, written.value()));
            ASSERT_TRUE(missing.has_value());
            EXPECT_FALSE(missing->exists);
            EXPECT_FALSE(missing->size_bytes.has_value());
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
            ASSERT_TRUE(std::filesystem::exists(assembled.locator));
            EXPECT_EQ(assembled.size_bytes, expected_content.size());
            EXPECT_EQ(std::filesystem::file_size(assembled.locator), static_cast<uintmax_t>(expected_content.size()));
            EXPECT_EQ(ReadBinaryFile(assembled.locator), expected_content);
            EXPECT_EQ(assembled.md5_hash, FileHashUtil::HashMd5(expected_content));
            EXPECT_EQ(assembled.sha256_hash, FileHashUtil::HashSha256(expected_content));
        }

        TEST_F(LocalFileStorageUploadConsistencyTest, AssembleChunksMissingChunkCleansTempArtifact) {
            const std::string upload_id = "assemble-missing-chunk";

            ASSERT_TRUE(drogon::sync_wait(m_storage->EnsureUploadTempDir(upload_id)).has_value());
            ASSERT_TRUE(drogon::sync_wait(m_storage->WriteChunk(upload_id, 0, "only-first-chunk")).has_value());

            auto assemble_result = drogon::sync_wait(m_storage->AssembleChunks(upload_id, 2));
            ASSERT_FALSE(assemble_result.has_value());
            EXPECT_EQ(assemble_result.error().code, ErrorCode::ChunkVerifyFailed);
            EXPECT_FALSE(std::filesystem::exists(AssembledPath(upload_id)));
        }

        TEST_F(LocalFileStorageAssemblyLogTest, AssembleSuccessRetainsCompleteContextAcrossBlockingQueue) {
            const std::string upload_id = "assemble-context-success";
            ASSERT_TRUE(drogon::sync_wait(m_storage->EnsureUploadTempDir(upload_id)).has_value());
            ASSERT_TRUE(drogon::sync_wait(m_storage->WriteChunk(upload_id, 0, "assembly-log-payload")).has_value());

            auto result = AssembleWithContext(
                upload_id,
                17,
                LogContext{
                    .request_id = "request-assembly-success",
                    .operation = "upload_complete",
                    .upload_id = upload_id,
                    .job_id = 41,
                    .lease_owner = "api-assembly-owner",
                    .state_version = 17,
                }
            );
            ASSERT_TRUE(result.has_value());

            const auto records = AssemblyRecords();
            ASSERT_EQ(records.size(), 4U);
            for (const auto& record : records) {
                EXPECT_EQ(record["level"].asString(), "debug");
                EXPECT_EQ(record["instance_id"].asString(), "local-assembly-test-instance");
                EXPECT_EQ(record["request_id"].asString(), "request-assembly-success");
                EXPECT_EQ(record["operation"].asString(), "upload_complete");
                EXPECT_EQ(record["upload_id"].asString(), upload_id);
                EXPECT_EQ(record["job_id"].asUInt64(), 41U);
                EXPECT_EQ(record["lease_owner"].asString(), "api-assembly-owner");
                EXPECT_EQ(record["state_version"].asUInt64(), 17U);
            }
            EXPECT_NE(records.back()["message"].asString().find("outcome=success"), std::string::npos);
        }

        TEST_F(LocalFileStorageAssemblyLogTest, AssembleFailureDoesNotInferMissingContextAcrossBlockingQueue) {
            const std::string upload_id = "assemble-context-failure";
            ASSERT_TRUE(drogon::sync_wait(m_storage->EnsureUploadTempDir(upload_id)).has_value());
            ASSERT_TRUE(drogon::sync_wait(m_storage->WriteChunk(upload_id, 0, "missing-after-write")).has_value());

            std::error_code error;
            ASSERT_TRUE(std::filesystem::remove(ChunkPath(upload_id, 0), error));
            ASSERT_FALSE(error);

            auto result = AssembleWithContext(
                upload_id,
                23,
                LogContext{
                    .request_id = "request-assembly-failure",
                    .operation = "upload_complete",
                }
            );
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::ChunkVerifyFailed);

            const auto records = AssemblyRecords();
            ASSERT_EQ(records.size(), 3U);
            for (size_t index = 0; index < records.size(); ++index) {
                const auto& record = records[index];
                EXPECT_EQ(record["level"].asString(), index < 2 ? "debug" : "info");
                EXPECT_EQ(record["instance_id"].asString(), "local-assembly-test-instance");
                EXPECT_EQ(record["request_id"].asString(), "request-assembly-failure");
                EXPECT_EQ(record["operation"].asString(), "upload_complete");
                for (const auto* field : {
                         "upload_id",
                         "job_id",
                         "lease_owner",
                         "state_version",
                     }) {
                    EXPECT_TRUE(record[field].isNull()) << field;
                }
            }
            EXPECT_NE(records.back()["message"].asString().find("outcome=failure"), std::string::npos);
        }

        TEST_F(LocalFileStorageUploadConsistencyTest, PromoteToFinalReportsCreatedForNewBlob) {
            const std::string upload_id = "promote-created";
            const std::string content = "promote-created-content";
            const auto hash = FileHashUtil::HashSha256(content);
            const auto temp_path = AssembledPath(upload_id);

            std::error_code ec;
            std::filesystem::create_directories(m_temp_base, ec);
            ASSERT_FALSE(ec);
            std::ofstream output(temp_path, std::ios::binary);
            output.write(content.data(), static_cast<std::streamsize>(content.size()));
            output.close();
            ASSERT_TRUE(output);

            const disk::storage::UploadStagingAssembly assembly{
                .backend = disk::storage::UploadStagingBackend::Local,
                .locator = temp_path.string(),
                .size_bytes = content.size(),
                .md5_hash = FileHashUtil::HashMd5(content),
                .sha256_hash = hash,
            };
            auto promote_result = drogon::sync_wait(m_blob_store->PromoteToFinal(assembly, hash));

            ASSERT_TRUE(promote_result.has_value());
            EXPECT_TRUE(promote_result->created);
            EXPECT_EQ(promote_result->path, FinalStoragePath(hash));
            EXPECT_TRUE(std::filesystem::exists(temp_path));
            ASSERT_TRUE(std::filesystem::exists(promote_result->path));
            EXPECT_EQ(ReadBinaryFile(promote_result->path), content);
        }

        TEST_F(LocalFileStorageUploadConsistencyTest, PromoteToFinalReportsReusedForPreexistingBlob) {
            const std::string upload_id = "promote-reused";
            const std::string content = "pre-existing-final-blob";
            const auto hash = FileHashUtil::HashSha256(content);
            const auto final_path = FinalStoragePath(hash);
            const auto temp_path = AssembledPath(upload_id);

            std::error_code ec;
            std::filesystem::create_directories(final_path.parent_path(), ec);
            ASSERT_FALSE(ec);
            std::ofstream final_output(final_path, std::ios::binary);
            final_output.write(content.data(), static_cast<std::streamsize>(content.size()));
            final_output.close();
            ASSERT_TRUE(final_output);

            std::filesystem::create_directories(m_temp_base, ec);
            ASSERT_FALSE(ec);
            std::ofstream temp_output(temp_path, std::ios::binary);
            temp_output.write(content.data(), static_cast<std::streamsize>(content.size()));
            temp_output.close();
            ASSERT_TRUE(temp_output);

            const disk::storage::UploadStagingAssembly assembly{
                .backend = disk::storage::UploadStagingBackend::Local,
                .locator = temp_path.string(),
                .size_bytes = content.size(),
                .md5_hash = FileHashUtil::HashMd5(content),
                .sha256_hash = hash,
            };
            auto promote_result = drogon::sync_wait(m_blob_store->PromoteToFinal(assembly, hash));

            ASSERT_TRUE(promote_result.has_value());
            EXPECT_FALSE(promote_result->created);
            EXPECT_EQ(promote_result->path, final_path);
            EXPECT_TRUE(std::filesystem::exists(temp_path));
            ASSERT_TRUE(std::filesystem::exists(final_path));
            EXPECT_EQ(ReadBinaryFile(final_path), content);
        }

    } // namespace
} // namespace disk::file
