#pragma once

#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../src/storage/LocalFileStorage.hpp"
#include "../../src/utils/FileHashUtil.hpp"

namespace disk::test_support {

    class UploadStagingTestAdapter final : public disk::storage::LocalFileStorage {
    public:
        using LocalFileStorage::LocalFileStorage;

        [[nodiscard]]
        auto EnsureUploadTempDir(const std::string& upload_id) -> drogon::Task<Result<void>> {
            co_return co_await LocalFileStorage::EnsureUploadSession(LocalSession(upload_id));
        }

        [[nodiscard]]
        auto WriteChunk(
            const std::string& upload_id,
            uint32_t chunk_index,
            std::string data
        ) -> drogon::Task<Result<disk::storage::UploadStagingChunk>> {
            const auto md5_hash = disk::utils::FileHashUtil::HashMd5(data);
            auto result = co_await LocalFileStorage::WriteChunk(
                LocalSession(upload_id),
                chunk_index,
                md5_hash,
                std::move(data)
            );
            if (result) {
                std::scoped_lock lock(m_mutex);
                m_chunks[upload_id][chunk_index] = result.value();
            }
            co_return result;
        }

        [[nodiscard]]
        auto WriteChunk(
            const std::string& upload_id,
            uint32_t chunk_index,
            const std::string& md5_hash,
            std::string data
        ) -> drogon::Task<Result<disk::storage::UploadStagingChunk>> {
            auto result = co_await LocalFileStorage::WriteChunk(
                LocalSession(upload_id),
                chunk_index,
                md5_hash,
                std::move(data)
            );
            if (result) {
                std::scoped_lock lock(m_mutex);
                m_chunks[upload_id][chunk_index] = result.value();
            }
            co_return result;
        }

        [[nodiscard]]
        auto AssembleChunks(const std::string& upload_id, uint32_t expected_chunk_count)
            -> drogon::Task<Result<disk::storage::UploadStagingAssembly>> {
            co_return co_await LocalFileStorage::AssembleChunks(
                LocalSession(upload_id),
                1,
                expected_chunk_count,
                DescriptorsFor(upload_id)
            );
        }

        [[nodiscard]]
        auto AssembleChunks(
            const std::string& upload_id,
            uint32_t expected_chunk_count,
            const std::vector<disk::storage::UploadStagingChunk>& chunks
        ) -> drogon::Task<Result<disk::storage::UploadStagingAssembly>> {
            co_return co_await LocalFileStorage::AssembleChunks(
                LocalSession(upload_id),
                1,
                expected_chunk_count,
                chunks
            );
        }

        [[nodiscard]]
        auto DiscardAssembly(
            const std::string& upload_id,
            const disk::storage::UploadStagingAssembly& assembly
        ) -> drogon::Task<Result<void>> {
            co_return co_await LocalFileStorage::DiscardAssembly(
                LocalSession(upload_id),
                assembly
            );
        }

        [[nodiscard]]
        auto CleanupTemp(const std::string& upload_id) -> drogon::Task<Result<void>> {
            co_return co_await LocalFileStorage::CleanupSession(LocalSession(upload_id));
        }

        [[nodiscard]]
        auto DescriptorsFor(const std::string& upload_id) const
            -> std::vector<disk::storage::UploadStagingChunk> {
            std::scoped_lock lock(m_mutex);
            std::vector<disk::storage::UploadStagingChunk> result;
            const auto upload = m_chunks.find(upload_id);
            if (upload == m_chunks.end()) {
                return result;
            }
            result.reserve(upload->second.size());
            for (const auto& [chunk_index, chunk] : upload->second) {
                (void)chunk_index;
                result.push_back(chunk);
            }
            return result;
        }

        [[nodiscard]]
        auto ChunkPath(
            const std::filesystem::path& staging_root,
            const std::string& upload_id,
            uint32_t chunk_index
        ) const -> std::filesystem::path {
            std::scoped_lock lock(m_mutex);
            const auto upload = m_chunks.find(upload_id);
            if (upload == m_chunks.end()) {
                return staging_root / upload_id / "missing-chunk";
            }
            const auto chunk = upload->second.find(chunk_index);
            if (chunk == upload->second.end()) {
                return staging_root / upload_id / "missing-chunk";
            }
            return staging_root / chunk->second.object_key;
        }

    private:
        [[nodiscard]]
        static auto LocalSession(const std::string& upload_id)
            -> disk::storage::UploadStagingSession {
            return disk::storage::UploadStagingSession{
                .upload_id = upload_id,
                .backend = disk::storage::UploadStagingBackend::Local,
                .prefix = upload_id,
            };
        }

        mutable std::mutex m_mutex;
        std::unordered_map<
            std::string,
            std::map<uint32_t, disk::storage::UploadStagingChunk>>
            m_chunks;
    };

} // namespace disk::test_support
