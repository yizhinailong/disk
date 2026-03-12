#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "storage/IFileStorage.hpp"

namespace disk::utils {
    class ConfigMgr;
}

namespace disk::storage {

    class LocalFileStorage : public IFileStorage {
    public:
        explicit LocalFileStorage(std::shared_ptr<disk::utils::ConfigMgr> config_mgr = nullptr);
        ~LocalFileStorage() override = default;
        LocalFileStorage(const LocalFileStorage&) = delete;
        auto operator=(const LocalFileStorage&) -> LocalFileStorage& = delete;
        LocalFileStorage(LocalFileStorage&&) = default;
        auto operator=(LocalFileStorage&&) -> LocalFileStorage& = default;

        [[nodiscard]]
        auto WriteChunk(const std::string& upload_id, uint32_t chunk_index, const std::string& data)
            -> drogon::Task<Result<void>> override;

        [[nodiscard]]
        auto AssembleChunks(const std::string& upload_id, uint32_t chunk_count)
            -> drogon::Task<Result<std::filesystem::path>> override;

        [[nodiscard]]
        auto PromoteToFinal(const std::filesystem::path& temp_path, const std::string& hash)
            -> drogon::Task<Result<std::filesystem::path>> override;

        [[nodiscard]]
        auto OpenForRead(const std::filesystem::path& storage_path)
            -> drogon::Task<Result<std::shared_ptr<std::ifstream>>> override;

        [[nodiscard]]
        auto DeletePath(const std::filesystem::path& target_path) -> drogon::Task<Result<void>> override;

        [[nodiscard]]
        auto CleanupTemp(const std::string& upload_id) -> drogon::Task<Result<void>> override;

        [[nodiscard]]
        auto Exists(const std::filesystem::path& target_path) -> drogon::Task<Result<bool>> override;

        [[nodiscard]]
        auto GetFinalStoragePath(const std::string& hash) const -> std::filesystem::path override;

        [[nodiscard]]
        auto GetFileSize(const std::filesystem::path& target_path) -> drogon::Task<Result<uint64_t>> override;

    private:
        [[nodiscard]]
        auto GetTempDirPath(const std::string& upload_id) const -> std::filesystem::path;

        [[nodiscard]]
        auto GetChunkFilePath(const std::string& upload_id, uint32_t chunk_index) const
            -> std::filesystem::path;

        [[nodiscard]]
        auto GetAssembleFilePath(const std::string& upload_id) const -> std::filesystem::path;

        std::shared_ptr<disk::utils::ConfigMgr> m_config_mgr;
    };

} // namespace disk::storage
