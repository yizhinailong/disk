#include "storage/LocalFileStorage.hpp"

#include <array>
#include <system_error>

#include <sodium/crypto_hash_sha256.h>
#include <trantor/utils/Logger.h>

#include "storage/AssemblyWorkerPool.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/FileHashUtil.hpp"

namespace disk::storage {

    using disk::utils::FileHashUtil;

    LocalFileStorage::LocalFileStorage(std::shared_ptr<disk::utils::ConfigMgr> config_mgr)
        : m_config_mgr(config_mgr == nullptr ? disk::utils::ConfigMgr::GetInstance() : std::move(config_mgr)) {}

    auto LocalFileStorage::WriteChunk(
        const std::string& upload_id,
        uint32_t chunk_index,
        const std::string& data
    ) -> drogon::Task<Result<void>> {
        const auto temp_dir = GetTempDirPath(upload_id);

        std::error_code ec;
        std::filesystem::create_directories(temp_dir, ec);
        if (ec) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to create temp upload directory")
            );
        }

        const auto chunk_path = GetChunkFilePath(upload_id, chunk_index);
        std::ofstream chunk_file(chunk_path, std::ios::binary);
        if (!chunk_file) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to open chunk file")
            );
        }

        chunk_file.write(data.data(), static_cast<std::streamsize>(data.size()));
        chunk_file.close();

        if (!chunk_file) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to write chunk file")
            );
        }

        co_return {};
    }

    auto LocalFileStorage::AssembleChunks(const std::string& upload_id, uint32_t chunk_count)
        -> drogon::Task<Result<AssembleResult>> {
        auto& pool = AssemblyWorkerPool::GetInstance();

        auto slot_guard = pool.TryAcquireGuard();
        if (!slot_guard.has_value()) {
            LOG_WARN << "Assembly concurrency limit reached, fast-failing: running="
                     << pool.RunningCount() << " pending=" << pool.PendingCount();
            co_return std::unexpected(
                ErrorInfo(ErrorCode::TooManyRequests, "Too many concurrent assembly operations, please retry later")
            );
        }

        LOG_DEBUG << "Assembly started: running=" << pool.RunningCount()
                  << " pending=" << pool.PendingCount();

        const auto assembled_path = GetAssembleFilePath(upload_id);
        const auto assembled_parent = assembled_path.parent_path();

        std::error_code ec;
        std::filesystem::create_directories(assembled_parent, ec);
        if (ec) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to prepare assemble directory")
            );
        }

        std::ofstream assembled_file(assembled_path, std::ios::binary);
        if (!assembled_file) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to create assembled temp file")
            );
        }

        FileHashUtil::Md5Context md5_ctx;
        FileHashUtil::Md5Init(md5_ctx);

        crypto_hash_sha256_state sha256_state;
        crypto_hash_sha256_init(&sha256_state);

        std::array<char, 8192> buffer{};
        for (uint32_t index = 0; index < chunk_count; ++index) {
            const auto chunk_path = GetChunkFilePath(upload_id, index);
            std::ifstream chunk_file(chunk_path, std::ios::binary);
            if (!chunk_file) {
                assembled_file.close();
                std::filesystem::remove(assembled_path, ec);
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to open chunk for assembling")
                );
            }

            while (chunk_file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()))) {
                auto bytes_read = static_cast<size_t>(chunk_file.gcount());
                assembled_file.write(buffer.data(), static_cast<std::streamsize>(bytes_read));
                FileHashUtil::Md5Update(md5_ctx, reinterpret_cast<const uint8_t*>(buffer.data()), bytes_read);
                crypto_hash_sha256_update(&sha256_state, reinterpret_cast<const unsigned char*>(buffer.data()), bytes_read);
            }
            if (chunk_file.gcount() > 0) {
                auto bytes_read = static_cast<size_t>(chunk_file.gcount());
                assembled_file.write(buffer.data(), static_cast<std::streamsize>(bytes_read));
                FileHashUtil::Md5Update(md5_ctx, reinterpret_cast<const uint8_t*>(buffer.data()), bytes_read);
                crypto_hash_sha256_update(&sha256_state, reinterpret_cast<const unsigned char*>(buffer.data()), bytes_read);
            }
            if (!chunk_file.eof()) {
                assembled_file.close();
                std::filesystem::remove(assembled_path, ec);
                co_return std::unexpected(
                    ErrorInfo(ErrorCode::InternalError, "Failed to read chunk for assembling")
                );
            }
        }

        assembled_file.close();
        if (!assembled_file) {
            std::filesystem::remove(assembled_path, ec);
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to write assembled temp file")
            );
        }

        uint8_t md5_digest[16];
        FileHashUtil::Md5Final(md5_ctx, md5_digest);

        std::array<uint8_t, crypto_hash_sha256_BYTES> sha256_digest{};
        crypto_hash_sha256_final(&sha256_state, sha256_digest.data());

        LOG_DEBUG << "Assembly completed: running=" << pool.RunningCount()
                  << " pending=" << pool.PendingCount();

        co_return AssembleResult{
            .path = assembled_path,
            .md5_hash = FileHashUtil::BytesToHex(md5_digest, 16),
            .sha256_hash = FileHashUtil::BytesToHex(sha256_digest.data(), sha256_digest.size())
        };
    }

    auto LocalFileStorage::PromoteToFinal(const std::filesystem::path& temp_path, const std::string& hash)
        -> drogon::Task<Result<std::filesystem::path>> {
        const auto final_path = GetFinalStoragePath(hash);
        const auto final_dir = final_path.parent_path();

        std::error_code ec;
        std::filesystem::create_directories(final_dir, ec);
        if (ec) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to create final storage directory")
            );
        }

        if (std::filesystem::exists(final_path, ec)) {
            std::filesystem::remove(temp_path, ec);
            co_return final_path;
        }
        ec.clear();

        std::filesystem::rename(temp_path, final_path, ec);
        if (!ec) {
            co_return final_path;
        }

        ec.clear();
        std::filesystem::copy_file(temp_path, final_path, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to move file to final storage path")
            );
        }

        std::filesystem::remove(temp_path, ec);
        if (ec) {
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to cleanup temp file after copy")
            );
        }

        co_return final_path;
    }

    auto LocalFileStorage::OpenForRead(const std::filesystem::path& storage_path)
        -> drogon::Task<Result<std::shared_ptr<std::ifstream>>> {
        auto stream = std::make_shared<std::ifstream>(storage_path, std::ios::binary);
        if (!*stream) {
            co_return std::unexpected(ErrorInfo(ErrorCode::FileReadError, "Failed to open file for reading"));
        }

        co_return stream;
    }

    auto LocalFileStorage::DeletePath(const std::filesystem::path& target_path)
        -> drogon::Task<Result<void>> {
        std::error_code ec;
        const bool exists = std::filesystem::exists(target_path, ec);
        if (ec) {
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Failed to check target path"));
        }
        if (!exists) {
            co_return {};
        }

        const bool is_directory = std::filesystem::is_directory(target_path, ec);
        if (ec) {
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Failed to inspect target path"));
        }

        if (is_directory) {
            std::filesystem::remove_all(target_path, ec);
        } else {
            std::filesystem::remove(target_path, ec);
        }

        if (ec) {
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Failed to delete target path"));
        }

        co_return {};
    }

    auto LocalFileStorage::CleanupTemp(const std::string& upload_id) -> drogon::Task<Result<void>> {
        const auto temp_dir = GetTempDirPath(upload_id);
        const auto assembled_file = GetAssembleFilePath(upload_id);

        std::error_code ec;
        std::filesystem::remove_all(temp_dir, ec);
        if (ec) {
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Failed to cleanup temp directory"));
        }

        std::filesystem::remove(assembled_file, ec);
        if (ec) {
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Failed to cleanup assembled temp file"));
        }

        co_return {};
    }

    auto LocalFileStorage::Exists(const std::filesystem::path& target_path)
        -> drogon::Task<Result<bool>> {
        std::error_code ec;
        const bool exists = std::filesystem::exists(target_path, ec);
        if (ec) {
            co_return std::unexpected(ErrorInfo(ErrorCode::InternalError, "Failed to check path existence"));
        }
        co_return exists;
    }

    auto LocalFileStorage::GetFinalStoragePath(const std::string& hash) const -> std::filesystem::path {
        return std::filesystem::path(m_config_mgr->GetStorageBasePath()) / hash.substr(0, 2) / (hash + ".bin");
    }

    auto LocalFileStorage::GetFileSize(const std::filesystem::path& target_path)
        -> drogon::Task<Result<uint64_t>> {
        std::error_code ec;
        const auto file_size = std::filesystem::file_size(target_path, ec);
        if (ec) {
            co_return std::unexpected(ErrorInfo(ErrorCode::FileReadError, "Failed to read file size"));
        }

        co_return file_size;
    }

    auto LocalFileStorage::GetTempDirPath(const std::string& upload_id) const -> std::filesystem::path {
        return std::filesystem::path(m_config_mgr->GetTempUploadPath()) / upload_id;
    }

    auto LocalFileStorage::GetChunkFilePath(const std::string& upload_id, uint32_t chunk_index) const
        -> std::filesystem::path {
        return GetTempDirPath(upload_id) / (std::to_string(chunk_index) + ".chunk");
    }

    auto LocalFileStorage::GetAssembleFilePath(const std::string& upload_id) const -> std::filesystem::path {
        return std::filesystem::path(m_config_mgr->GetTempUploadPath()) / (upload_id + ".tmp");
    }

} // namespace disk::storage
