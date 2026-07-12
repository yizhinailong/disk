/**
 * @file UploadPath_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Upload chunk data flow characterization tests (T5 profiling)
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Characterization tests recording current upload behavior and profiling
 * the exact memory copy count from request body to disk.
 *
 * ========================================================================
 * UPLOAD CHUNK DATA FLOW PROFILING (as of T5 analysis)
 * ========================================================================
 *
 * Data flow diagram:
 *
 *   request->body()                                          [Drogon buffer]
 *        │                                                        │
 *        │  (zero-copy string_view)                               │
 *        ▼                                                        │
 *   FileController::UploadChunk()                                 │
 *     const auto chunk_data = request->body()  ← string_view ────┘
 *        │
 *        │  (string_view passed by value — pointer+size only)
 *        ▼
 *   FileService::UploadChunk(upload_id, chunk_index,
 *                            chunk_hash, chunk_data, user_id)
 *        │
 *        │  COPY #1: std::string chunk_payload{chunk_data}
 *        │  File: src/services/FileService.cpp:927
 *        │  Action: heap allocation + memcpy of full chunk body
 *        │  Size: up to 5 MB per chunk (config chunk_size)
 *        │
 *        │  HashMd5(chunk_payload) — reads via const& — no copy
 *        │  File: src/utils/FileHashUtil.hpp:54-63
 *        │
 *        │  std::move(chunk_payload) — ownership transfer — no copy
 *        │  File: src/services/FileService.cpp:947
 *        ▼
 *   LocalFileStorage::WriteChunk(upload_id, chunk_index, data)
 *     data is std::string (moved in)
 *        │
 *        │  Lambda capture: chunk_data = std::move(data) — no copy
 *        │  File: src/storage/LocalFileStorage.cpp:243
 *        │
 *        │  ofstream::write(chunk_data.data(), size) — no copy
 *        │  File: src/storage/LocalFileStorage.cpp:262-264
 *        ▼
 *   Disk (temp chunk file)
 *
 * ========================================================================
 * MEMORY COPY COUNT SUMMARY
 * ========================================================================
 *
 * Chunk data copies:     1 (the string_view → std::string conversion)
 * Chunk data moves:      2 (service→storage param, lambda capture)
 * Chunk data allocations:1 (the std::string heap allocation)
 *
 * Small parameter copies (not chunk data):
 *   - upload_id:   std::string(getParameter("upload_id"))  ~36 chars
 *   - chunk_index: std::string(getParameter("chunk_index")) ~1-2 chars
 *   - chunk_hash:  std::string(getParameter("chunk_hash"))  32 chars
 *   Total: ~70 chars of parameter copies (negligible)
 *
 * ========================================================================
 * HASH COMPUTATION ANALYSIS
 * ========================================================================
 *
 * FileHashUtil::HashMd5 takes const std::string& — no ownership needed.
 * Internally only uses .data() and .length(), both available on string_view.
 * The hash API COULD accept string_view without any functional change.
 * However: the copy at line 927 is REQUIRED regardless, because:
 *   1) WriteChunk takes std::string by value (ownership needed for async path)
 *   2) The original string_view may not survive across co_await points
 *   3) Hash verification must happen BEFORE writing to disk
 * Conclusion: changing HashMd5 to accept string_view would NOT eliminate
 * the copy; the copy is forced by WriteChunk's ownership requirement.
 *
 * ========================================================================
 * WHY THE COPY IS NECESSARY
 * ========================================================================
 *
 * 1. request->body() returns string_view into Drogon's internal buffer.
 *    This buffer is valid only during the synchronous controller phase.
 * 2. FileService::UploadChunk is a coroutine (co_await points exist).
 *    The string_view could dangle after any co_await.
 * 3. WriteChunk offloads to a thread pool via RunBlockingFilesystemTask.
 *    The data must outlive the co_await that submits the task.
 * 4. The std::string is created BEFORE the first co_await (hash check),
 *    then moved through to the thread pool lambda — zero additional copies.
 *
 * ========================================================================
 */

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <drogon/drogon.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>

#include "../../src/dtos/FileDto.hpp"
#include "../../src/storage/LocalFileStorage.hpp"
#include "../../src/utils/ConfigMgr.hpp"
#include "../../src/utils/FileHashUtil.hpp"

/// LocalFileStorage.cpp implementation is provided by FileUploadConsistency_test.cpp
/// in the same disk-test binary; do not include it again (ODR violation).

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

        /// Generate deterministic content of given size using a simple pattern
        auto MakePattern(size_t size, uint8_t seed = 0xAA) -> std::string {
            std::string result(size, '\0');
            for (size_t i = 0; i < size; ++i) {
                result[i] = static_cast<char>(static_cast<uint8_t>(seed ^ (i & 0xFF)));
            }
            return result;
        }

        class UploadPathTest : public ::testing::Test {
        protected:
            void SetUp() override {
                const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
                m_root = std::filesystem::path("build/test_upload_path") /
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
        };

        /// =====================================================================
        /// 1. Upload Init: DTO contract and storage directory creation
        /// =====================================================================

        class UploadPathInitTest : public ::testing::Test {};

        TEST_F(UploadPathInitTest, InitUploadRequestValidatesFilenameCorrectly) {
            /// Simulate what FileController does: parse JSON, validate DTO
            auto req = drogon::HttpRequest::newHttpJsonRequest(
                [] {
                    Json::Value json;
                    json["filename"] = "test_file.dat";
                    json["file_size"] = 10485760;  ///< 10 MB
                    json["file_hash"] = "d41d8cd98f00b204e9800998ecf8427e";
                    json["parent_id"] = 0;
                    return json;
                }()
            );
            req->setMethod(drogon::Post);

            auto result = InitUploadRequest::FromRequest(req);
            ASSERT_TRUE(result.has_value());
            EXPECT_EQ(result->filename, "test_file.dat");
            EXPECT_EQ(result->file_size, 10485760U);
            EXPECT_EQ(result->file_hash, "d41d8cd98f00b204e9800998ecf8427e");
            EXPECT_EQ(result->parent_id, 0U);
        }

        TEST_F(UploadPathInitTest, InitUploadRequestRejectsInvalidHash) {
            auto req = drogon::HttpRequest::newHttpJsonRequest(
                [] {
                    Json::Value json;
                    json["filename"] = "test.bin";
                    json["file_size"] = 1024;
                    json["file_hash"] = "not-a-valid-hash";
                    return json;
                }()
            );
            req->setMethod(drogon::Post);

            auto result = InitUploadRequest::FromRequest(req);
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
        }

        TEST_F(UploadPathInitTest, InitUploadRequestRejectsZeroFileSize) {
            auto req = drogon::HttpRequest::newHttpJsonRequest(
                [] {
                    Json::Value json;
                    json["filename"] = "empty.bin";
                    json["file_size"] = 0;
                    json["file_hash"] = "d41d8cd98f00b204e9800998ecf8427e";
                    return json;
                }()
            );
            req->setMethod(drogon::Post);

            auto result = InitUploadRequest::FromRequest(req);
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
        }

        TEST_F(UploadPathInitTest, InitUploadResponseSerializesCorrectly) {
            InitUploadResponse response;
            response.upload_id = "task-abc-123";
            response.chunk_size = 5 * 1024 * 1024;
            response.total_chunks = 4;
            response.uploaded_chunks = { 0, 2 };
            response.instant_upload = false;

            const auto json = response.ToJson();

            EXPECT_EQ(json["upload_id"].asString(), "task-abc-123");
            EXPECT_EQ(json["chunk_size"].asUInt(), 5U * 1024U * 1024U);
            EXPECT_EQ(json["total_chunks"].asUInt(), 4U);
            EXPECT_FALSE(json["instant_upload"].asBool());
            ASSERT_EQ(json["uploaded_chunks"].size(), 2U);
            EXPECT_EQ(json["uploaded_chunks"][0].asUInt(), 0U);
            EXPECT_EQ(json["uploaded_chunks"][1].asUInt(), 2U);
        }

        TEST_F(UploadPathTest, EnsureUploadTempDirCreatesDirectoryStructure) {
            const std::string upload_id = "init-temp-dir-test";
            auto result = drogon::sync_wait(m_storage->EnsureUploadTempDir(upload_id));

            ASSERT_TRUE(result.has_value());
            EXPECT_TRUE(std::filesystem::exists(m_temp_base / upload_id));
            EXPECT_TRUE(std::filesystem::is_directory(m_temp_base / upload_id));
        }

        /// =====================================================================
        /// 2. Chunk Upload: single chunk write and verification
        /// =====================================================================

        TEST_F(UploadPathTest, WriteSingleChunkPersistsExactBytesOnDisk) {
            const std::string upload_id = "single-chunk-test";
            const std::string chunk_data = MakePattern(4096, 0x42);

            ASSERT_TRUE(drogon::sync_wait(m_storage->EnsureUploadTempDir(upload_id)).has_value());

            auto result = drogon::sync_wait(m_storage->WriteChunk(upload_id, 0, chunk_data));
            ASSERT_TRUE(result.has_value());

            const auto chunk_path = ChunkPath(upload_id, 0);
            ASSERT_TRUE(std::filesystem::exists(chunk_path));
            EXPECT_EQ(std::filesystem::file_size(chunk_path), static_cast<uintmax_t>(chunk_data.size()));

            const auto on_disk = ReadBinaryFile(chunk_path);
            EXPECT_EQ(on_disk, chunk_data);
        }

        TEST_F(UploadPathTest, WriteChunkWithEmbeddedNullsPreservesBinaryIntegrity) {
            const std::string upload_id = "binary-null-test";
            /// Data with embedded null bytes
            const std::string chunk_data = std::string("AB\0CD\0\0EF", 9) + MakePattern(1000, 0xFF);

            ASSERT_TRUE(drogon::sync_wait(m_storage->EnsureUploadTempDir(upload_id)).has_value());

            auto result = drogon::sync_wait(m_storage->WriteChunk(upload_id, 0, chunk_data));
            ASSERT_TRUE(result.has_value());

            const auto on_disk = ReadBinaryFile(ChunkPath(upload_id, 0));
            ASSERT_EQ(on_disk.size(), chunk_data.size());
            EXPECT_EQ(on_disk, chunk_data);
        }

        TEST_F(UploadPathTest, WriteChunkOverwritesExistingChunk) {
            const std::string upload_id = "overwrite-test";
            const std::string first_data = MakePattern(2048, 0x11);
            const std::string second_data = MakePattern(1024, 0x22);

            ASSERT_TRUE(drogon::sync_wait(m_storage->EnsureUploadTempDir(upload_id)).has_value());

            ASSERT_TRUE(drogon::sync_wait(m_storage->WriteChunk(upload_id, 0, first_data)).has_value());
            ASSERT_TRUE(drogon::sync_wait(m_storage->WriteChunk(upload_id, 0, second_data)).has_value());

            /// Second write should have overwritten the first
            const auto on_disk = ReadBinaryFile(ChunkPath(upload_id, 0));
            EXPECT_EQ(on_disk, second_data);
            EXPECT_EQ(std::filesystem::file_size(ChunkPath(upload_id, 0)), static_cast<uintmax_t>(1024));
        }

        /// =====================================================================
        /// 3. Multiple Chunks: sequential writes and independent verification
        /// =====================================================================

        TEST_F(UploadPathTest, WriteMultipleChunksCreatesSeparateFiles) {
            const std::string upload_id = "multi-chunk-test";
            const std::vector<std::string> chunks = {
                MakePattern(2048, 0x10),
                MakePattern(2048, 0x20),
                MakePattern(1024, 0x30),
            };

            ASSERT_TRUE(drogon::sync_wait(m_storage->EnsureUploadTempDir(upload_id)).has_value());

            for (uint32_t i = 0; i < chunks.size(); ++i) {
                auto result = drogon::sync_wait(m_storage->WriteChunk(
                    upload_id, i, chunks[i]
                ));
                ASSERT_TRUE(result.has_value()) << "Failed to write chunk " << i;
            }

            /// Verify each chunk independently
            for (uint32_t i = 0; i < chunks.size(); ++i) {
                const auto path = ChunkPath(upload_id, i);
                ASSERT_TRUE(std::filesystem::exists(path)) << "Chunk " << i << " missing";
                EXPECT_EQ(ReadBinaryFile(path), chunks[i]) << "Chunk " << i << " content mismatch";
            }
        }

        TEST_F(UploadPathTest, WriteChunkOutOfOrderSucceeds) {
            /// Chunks may arrive out of order in practice
            const std::string upload_id = "out-of-order-test";
            const std::vector<std::string> chunks = {
                MakePattern(512, 0xA0),
                MakePattern(512, 0xA1),
                MakePattern(512, 0xA2),
            };

            ASSERT_TRUE(drogon::sync_wait(m_storage->EnsureUploadTempDir(upload_id)).has_value());

            /// Write in reverse order: chunk 2, then 0, then 1
            ASSERT_TRUE(drogon::sync_wait(m_storage->WriteChunk(upload_id, 2, chunks[2])).has_value());
            ASSERT_TRUE(drogon::sync_wait(m_storage->WriteChunk(upload_id, 0, chunks[0])).has_value());
            ASSERT_TRUE(drogon::sync_wait(m_storage->WriteChunk(upload_id, 1, chunks[1])).has_value());

            for (uint32_t i = 0; i < chunks.size(); ++i) {
                EXPECT_EQ(ReadBinaryFile(ChunkPath(upload_id, i)), chunks[i]);
            }
        }

        /// =====================================================================
        /// 4. Upload Complete: assembly and hash verification
        /// =====================================================================

        TEST_F(UploadPathTest, AssembleChunksProducesCorrectMergedContent) {
            const std::string upload_id = "assemble-correct-test";
            const std::vector<std::string> chunks = {
                "header-bytes-",
                MakePattern(2048, 0x55),
                std::string("trailer"),
            };
            const std::string expected_content = chunks[0] + chunks[1] + chunks[2];

            ASSERT_TRUE(drogon::sync_wait(m_storage->EnsureUploadTempDir(upload_id)).has_value());
            for (uint32_t i = 0; i < chunks.size(); ++i) {
                ASSERT_TRUE(drogon::sync_wait(m_storage->WriteChunk(upload_id, i, chunks[i])).has_value());
            }

            auto result = drogon::sync_wait(
                m_storage->AssembleChunks(upload_id, static_cast<uint32_t>(chunks.size()))
            );
            ASSERT_TRUE(result.has_value());

            const auto& assembled = result.value();
            EXPECT_TRUE(std::filesystem::exists(assembled.path));
            EXPECT_EQ(std::filesystem::file_size(assembled.path), expected_content.size());
            EXPECT_EQ(ReadBinaryFile(assembled.path), expected_content);
        }

        TEST_F(UploadPathTest, AssembleChunksProducesCorrectMd5AndSha256) {
            const std::string upload_id = "assemble-hash-test";
            const std::vector<std::string> chunks = {
                MakePattern(1024, 0xBB),
                MakePattern(1024, 0xCC),
            };
            const std::string expected_content = chunks[0] + chunks[1];

            ASSERT_TRUE(drogon::sync_wait(m_storage->EnsureUploadTempDir(upload_id)).has_value());
            for (uint32_t i = 0; i < chunks.size(); ++i) {
                ASSERT_TRUE(drogon::sync_wait(m_storage->WriteChunk(upload_id, i, chunks[i])).has_value());
            }

            auto result = drogon::sync_wait(
                m_storage->AssembleChunks(upload_id, static_cast<uint32_t>(chunks.size()))
            );
            ASSERT_TRUE(result.has_value());

            const auto& assembled = result.value();
            EXPECT_EQ(assembled.md5_hash, FileHashUtil::HashMd5(expected_content));
            EXPECT_EQ(assembled.sha256_hash, FileHashUtil::HashSha256(expected_content));
        }

        TEST_F(UploadPathTest, AssembleChunksMissingChunkReturnsErrorAndCleansUp) {
            const std::string upload_id = "assemble-missing-test";

            ASSERT_TRUE(drogon::sync_wait(m_storage->EnsureUploadTempDir(upload_id)).has_value());
            ASSERT_TRUE(drogon::sync_wait(
                m_storage->WriteChunk(upload_id, 0, MakePattern(1024))
            ).has_value());
            /// Chunk 1 is intentionally missing

            auto result = drogon::sync_wait(m_storage->AssembleChunks(upload_id, 2));
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::InternalError);

            /// Temp artifact must not be left behind
            EXPECT_FALSE(std::filesystem::exists(AssembledPath(upload_id)));
        }

        /// =====================================================================
        /// 5. Hash Verification: MD5 hash computation on chunk data
        /// =====================================================================

        TEST_F(UploadPathTest, HashMd5ComputesCorrectHash) {
            /// Verify that the hash computed in FileService (via HashMd5)
            /// matches what we'd compute independently
            const std::string data = MakePattern(5 * 1024 * 1024, 0x77);  ///< 5 MB (default chunk size)
            const auto hash = FileHashUtil::HashMd5(data);

            /// Hash must be 32-character lowercase hex
            EXPECT_EQ(hash.length(), 32U);
            for (char c : hash) {
                EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
            }

            /// Same input must produce same hash
            EXPECT_EQ(FileHashUtil::HashMd5(data), hash);
        }

        TEST_F(UploadPathTest, HashMd5IsDeterministicAcrossCalls) {
            const std::string data = "The quick brown fox jumps over the lazy dog";
            const auto hash1 = FileHashUtil::HashMd5(data);
            const auto hash2 = FileHashUtil::HashMd5(data);

            /// Well-known MD5 for this string: 9e107d9d372bb6826bd81d3542a419d6
            EXPECT_EQ(hash1, hash2);
            EXPECT_EQ(hash1, "9e107d9d372bb6826bd81d3542a419d6");
        }

        TEST_F(UploadPathTest, HashMd5HandlesEmptyString) {
            const std::string data;
            const auto hash = FileHashUtil::HashMd5(data);

            /// Well-known MD5 of empty string: d41d8cd98f00b204e9800998ecf8427e
            EXPECT_EQ(hash, "d41d8cd98f00b204e9800998ecf8427e");
        }

        TEST_F(UploadPathTest, HashMd5HandlesBinaryDataWithNulls) {
            const std::string data = std::string("\x00\x01\x02\x03", 4);
            const auto hash = FileHashUtil::HashMd5(data);

            EXPECT_EQ(hash.length(), 32U);
            /// Verify it's not the empty-string hash
            EXPECT_NE(hash, "d41d8cd98f00b204e9800998ecf8427e");
        }

        TEST_F(UploadPathTest, VerifyHashMatchesCorrectly) {
            const std::string data = MakePattern(4096);
            const auto expected_hash = FileHashUtil::HashMd5(data);

            EXPECT_TRUE(FileHashUtil::VerifyHash(data, expected_hash));
            EXPECT_FALSE(FileHashUtil::VerifyHash(data, "0123456789abcdef0123456789abcdef"));
        }

        /// =====================================================================
        /// 6. End-to-end: full init → chunk → assemble → promote pipeline
        /// =====================================================================

        TEST_F(UploadPathTest, FullPipelineInitChunkAssemblePromote) {
            const std::string upload_id = "full-pipeline-test";
            const std::vector<std::string> chunks = {
                MakePattern(2048, 0xDD),
                MakePattern(2048, 0xEE),
            };
            const std::string expected_content = chunks[0] + chunks[1];
            const auto expected_md5 = FileHashUtil::HashMd5(expected_content);

            /// Step 1: Init — ensure temp directory
            auto init_result = drogon::sync_wait(m_storage->EnsureUploadTempDir(upload_id));
            ASSERT_TRUE(init_result.has_value());
            EXPECT_TRUE(std::filesystem::exists(m_temp_base / upload_id));

            /// Step 2: Upload chunks
            for (uint32_t i = 0; i < chunks.size(); ++i) {
                /// Compute chunk hash (as FileService does)
                const auto chunk_hash = FileHashUtil::HashMd5(chunks[i]);

                /// Verify hash matches before writing
                EXPECT_EQ(chunk_hash, FileHashUtil::HashMd5(chunks[i]));

                auto write_result = drogon::sync_wait(
                    m_storage->WriteChunk(upload_id, i, chunks[i])
                );
                ASSERT_TRUE(write_result.has_value()) << "WriteChunk " << i << " failed";
            }

            /// Verify all chunk files on disk
            for (uint32_t i = 0; i < chunks.size(); ++i) {
                EXPECT_TRUE(std::filesystem::exists(ChunkPath(upload_id, i)));
            }

            /// Step 3: Assemble
            auto assemble_result = drogon::sync_wait(
                m_storage->AssembleChunks(upload_id, static_cast<uint32_t>(chunks.size()))
            );
            ASSERT_TRUE(assemble_result.has_value());

            const auto& assembled = assemble_result.value();
            EXPECT_EQ(ReadBinaryFile(assembled.path), expected_content);
            EXPECT_EQ(assembled.md5_hash, expected_md5);

            /// Step 4: Promote to final storage
            auto promote_result = drogon::sync_wait(
                m_storage->PromoteToFinal(assembled.path, assembled.md5_hash)
            );
            ASSERT_TRUE(promote_result.has_value());

            const auto& promoted = promote_result.value();
            EXPECT_TRUE(promoted.created);
            const auto& final_path = promoted.path;
            EXPECT_TRUE(std::filesystem::exists(final_path));
            EXPECT_EQ(ReadBinaryFile(final_path), expected_content);

            /// Verify final storage path matches expected pattern
            const auto expected_final = m_storage->GetFinalStoragePath(assembled.md5_hash);
            EXPECT_EQ(final_path, expected_final);

            /// Step 5: Cleanup temp
            auto cleanup_result = drogon::sync_wait(m_storage->CleanupTemp(upload_id));
            ASSERT_TRUE(cleanup_result.has_value());
            EXPECT_FALSE(std::filesystem::exists(m_temp_base / upload_id));
        }

        TEST_F(UploadPathTest, FullPipelineSingleChunkFile) {
            /// Edge case: file smaller than chunk_size (1 chunk total)
            const std::string upload_id = "single-chunk-pipeline";
            const std::string content = MakePattern(1024, 0x99);
            const auto expected_md5 = FileHashUtil::HashMd5(content);

            ASSERT_TRUE(drogon::sync_wait(m_storage->EnsureUploadTempDir(upload_id)).has_value());
            ASSERT_TRUE(drogon::sync_wait(
                m_storage->WriteChunk(upload_id, 0, content)
            ).has_value());

            auto assemble_result = drogon::sync_wait(m_storage->AssembleChunks(upload_id, 1));
            ASSERT_TRUE(assemble_result.has_value());

            EXPECT_EQ(ReadBinaryFile(assemble_result->path), content);
            EXPECT_EQ(assemble_result->md5_hash, expected_md5);
        }

        /// =====================================================================
        /// 7. DTO serialization for upload complete
        /// =====================================================================

        class UploadPathDtoTest : public ::testing::Test {};

        TEST_F(UploadPathDtoTest, UploadChunkResponseSerializesCorrectly) {
            UploadChunkResponse response;
            response.chunk_index = 5;
            response.uploaded = true;

            const auto json = response.ToJson();

            EXPECT_EQ(json["chunk_index"].asUInt(), 5U);
            EXPECT_TRUE(json["uploaded"].asBool());
        }

        TEST_F(UploadPathDtoTest, CompleteUploadRequestParsesValidInput) {
            auto req = drogon::HttpRequest::newHttpJsonRequest(
                [] {
                    Json::Value json;
                    json["upload_id"] = "upload-xyz-789";
                    return json;
                }()
            );
            req->setMethod(drogon::Post);

            auto result = CompleteUploadRequest::FromRequest(req);
            ASSERT_TRUE(result.has_value());
            EXPECT_EQ(result->upload_id, "upload-xyz-789");
        }

        TEST_F(UploadPathDtoTest, CompleteUploadRequestRejectsEmptyUploadId) {
            auto req = drogon::HttpRequest::newHttpJsonRequest(
                [] {
                    Json::Value json;
                    json["upload_id"] = "";
                    return json;
                }()
            );
            req->setMethod(drogon::Post);

            auto result = CompleteUploadRequest::FromRequest(req);
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ErrorCode::ValidationFailed);
        }

        TEST_F(UploadPathDtoTest, CompleteUploadResponseContainsFileItem) {
            CompleteUploadResponse response;
            response.file = FileItem{
                .id = 42,
                .name = "uploaded.dat",
                .size = 65536,
                .hash = "abc123def456abc123def456abc123de",
                .mime_type = "application/octet-stream",
                .parent_id = 0,
                .created_at = "2026-05-24 12:00:00"
            };

            const auto json = response.ToJson();

            ASSERT_TRUE(json.isMember("file"));
            EXPECT_EQ(json["file"]["id"].asUInt64(), 42U);
            EXPECT_EQ(json["file"]["name"].asString(), "uploaded.dat");
            EXPECT_EQ(json["file"]["size"].asUInt64(), 65536U);
            EXPECT_EQ(json["file"]["hash"].asString(), "abc123def456abc123def456abc123de");
        }

        /// =====================================================================
        /// 8. Move semantics verification (characterizes the zero-copy path)
        /// =====================================================================

        TEST_F(UploadPathTest, WriteChunkAcceptsMovedString) {
            /// Characterize that WriteChunk takes std::string by value,
            /// meaning callers can std::move their string to avoid copies.
            const std::string upload_id = "move-semantics-test";
            const std::string original_data = MakePattern(8192, 0x55);
            std::string data_copy = original_data;

            ASSERT_TRUE(drogon::sync_wait(m_storage->EnsureUploadTempDir(upload_id)).has_value());

            /// Move the string into WriteChunk (as FileService does at line 947)
            auto result = drogon::sync_wait(
                m_storage->WriteChunk(upload_id, 0, std::move(data_copy))
            );
            ASSERT_TRUE(result.has_value());

            /// Verify the data on disk matches the original
            EXPECT_EQ(ReadBinaryFile(ChunkPath(upload_id, 0)), original_data);
        }

        /// =====================================================================
        /// 9. Cleanup verification
        /// =====================================================================

        TEST_F(UploadPathTest, CleanupTempRemovesUploadDirectory) {
            const std::string upload_id = "cleanup-test";

            ASSERT_TRUE(drogon::sync_wait(m_storage->EnsureUploadTempDir(upload_id)).has_value());
            ASSERT_TRUE(drogon::sync_wait(
                m_storage->WriteChunk(upload_id, 0, MakePattern(512))
            ).has_value());

            EXPECT_TRUE(std::filesystem::exists(m_temp_base / upload_id));

            auto result = drogon::sync_wait(m_storage->CleanupTemp(upload_id));
            ASSERT_TRUE(result.has_value());
            EXPECT_FALSE(std::filesystem::exists(m_temp_base / upload_id));
        }

        TEST_F(UploadPathTest, CleanupTempIdempotentOnMissingDirectory) {
            const std::string upload_id = "cleanup-nonexistent";
            /// Never created the directory
            EXPECT_FALSE(std::filesystem::exists(m_temp_base / upload_id));

            auto result = drogon::sync_wait(m_storage->CleanupTemp(upload_id));
            /// Should succeed even if directory doesn't exist
            ASSERT_TRUE(result.has_value());
        }

    } ///< namespace
} ///< namespace disk::file
