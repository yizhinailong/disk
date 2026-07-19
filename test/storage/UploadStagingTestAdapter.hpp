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
        using LocalFileStorage::AssembleChunks;
        using LocalFileStorage::LocalFileStorage;
        using LocalFileStorage::WriteChunk;

        [[nodiscard]]
        auto WriteChunk(
            const std::string& upload_id,
            uint32_t chunk_index,
            std::string data
        ) -> drogon::Task<Result<disk::storage::UploadStagingChunk>> {
            const auto md5_hash = disk::utils::FileHashUtil::HashMd5(data);
            auto result = co_await LocalFileStorage::WriteChunk(
                upload_id,
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
                upload_id,
                expected_chunk_count,
                DescriptorsFor(upload_id)
            );
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
        mutable std::mutex m_mutex;
        std::unordered_map<
            std::string,
            std::map<uint32_t, disk::storage::UploadStagingChunk>>
            m_chunks;
    };

} // namespace disk::test_support
