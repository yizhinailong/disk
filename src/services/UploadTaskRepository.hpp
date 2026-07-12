/**
 * @file UploadTaskRepository.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 上传任务持久化原语
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "models/UploadTasks.hpp"
#include "services/UploadLifecycleService.hpp"

namespace disk::file {

    struct ExpiredUploadTaskRecord {
        std::string id;
        std::string temp_path;
        uint64_t user_id{0};
        uint64_t reserved_bytes{0};
    };

    /**
     * @brief 上传任务持久化原语
     */
    class UploadTaskRepository {
    public:
        explicit UploadTaskRepository(drogon::orm::DbClientPtr db_client);

        [[nodiscard]]
        auto FindById(const std::string& upload_id) const
            -> drogon::Task<std::optional<drogon_model::disk::UploadTasks>>;

        [[nodiscard]]
        auto FindByIdForUser(const std::string& upload_id, uint64_t user_id) const
            -> drogon::Task<std::optional<drogon_model::disk::UploadTasks>>;

        [[nodiscard]]
        auto FindInProgressByUserAndHash(uint64_t user_id, const std::string& file_hash) const
            -> drogon::Task<std::optional<drogon_model::disk::UploadTasks>>;

        [[nodiscard]]
        auto FindInProgressIdByUserAndHash(uint64_t user_id, const std::string& file_hash) const
            -> drogon::Task<std::optional<std::string>>;

        [[nodiscard]]
        auto Create(drogon_model::disk::UploadTasks task) const
            -> drogon::Task<drogon_model::disk::UploadTasks>;

        [[nodiscard]]
        auto DeleteInProgressById(const std::string& upload_id) const -> drogon::Task<bool>;

        [[nodiscard]]
        auto MarkCompleted(std::string const& upload_id) const -> drogon::Task<bool>;

        [[nodiscard]]
        auto MarkCompletedIfInProgress(
            const drogon::orm::DbClientPtr& client,
            const std::string& upload_id
        ) const -> drogon::Task<bool>;

        [[nodiscard]]
        auto MarkCancelledIfInProgress(
            const std::string& upload_id,
            const std::string& fail_reason
        ) const -> drogon::Task<bool>;

        [[nodiscard]]
        auto MarkExpiredIfInProgressBatch(
            const std::vector<std::string>& upload_ids,
            const std::string& fail_reason
        ) const -> drogon::Task<uint64_t>;

        [[nodiscard]]
        auto MarkExpiredIfInProgressReturning(
            const drogon::orm::DbClientPtr& client,
            const std::string& upload_id,
            const std::string& fail_reason
        ) const -> drogon::Task<std::optional<ExpiredUploadTaskRecord>>;

        [[nodiscard]]
        auto RecordChunkUploadedIfAbsent(const std::string& upload_id, uint32_t chunk_index) const
            -> drogon::Task<bool>;

        [[nodiscard]]
        auto ListUploadedChunkIndices(const std::string& upload_id) const
            -> drogon::Task<std::vector<uint32_t>>;

        [[nodiscard]]
        auto GetChunkCoverage(const std::string& upload_id) const
            -> drogon::Task<disk::upload::ChunkCoverage>;

        auto DeleteChunks(std::string const& upload_id) const -> drogon::Task<void>;

        auto DeleteChunks(
            const drogon::orm::DbClientPtr& client,
            const std::string& upload_id
        ) const -> drogon::Task<void>;

        [[nodiscard]]
        auto FindExpiredInProgressBatch(size_t limit) const
            -> drogon::Task<std::vector<ExpiredUploadTaskRecord>>;

    private:
        drogon::orm::DbClientPtr m_db_client;
    };

} ///< namespace disk::file
