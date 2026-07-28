/**
 * @file ContentService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件内容领域服务
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <drogon/orm/DbClient.h>

#include "utils/ErrorCode.hpp"
#include "utils/LogHelper.hpp"

namespace disk::content {

    struct ContentMetadata {
        uint64_t id{ 0 };
        std::string hash_md5;
        std::string hash_sha256;
        uint64_t size{ 0 };
        std::string storage_path;
        std::string mime_type;
        int ref_count{ 0 };
    };

    struct NewContent {
        std::string hash_md5;
        std::string hash_sha256;
        uint64_t size{ 0 };
        std::string storage_path;
        std::string mime_type;
    };

    /**
     * @brief 文件内容领域服务
     *
     * 集中封装 file_contents 的查找、创建、引用计数更新与 Blob GC 任务创建。
     */
    class ContentService {
    public:
        explicit ContentService(drogon::orm::DbClientPtr db_client);
        ~ContentService() = default;
        ContentService(const ContentService&) = delete;
        auto operator=(const ContentService&) -> ContentService& = delete;
        ContentService(ContentService&&) = default;
        auto operator=(ContentService&&) -> ContentService& = default;

        [[nodiscard]]
        auto FindByMd5(
            const std::string& hash_md5,
            disk::utils::LogContext log_context = {}
        ) const
            -> drogon::Task<std::optional<ContentMetadata>>;

        [[nodiscard]]
        auto FindByMd5(
            const drogon::orm::DbClientPtr& client,
            const std::string& hash_md5,
            disk::utils::LogContext log_context = {}
        ) const
            -> drogon::Task<std::optional<ContentMetadata>>;

        [[nodiscard]]
        auto FindExistingIds(
            const drogon::orm::DbClientPtr& client,
            const std::vector<uint64_t>& content_ids,
            disk::utils::LogContext log_context = {}
        ) const -> drogon::Task<std::unordered_set<uint64_t>>;

        [[nodiscard]]
        auto AcquireReference(
            const drogon::orm::DbClientPtr& client,
            const NewContent& content,
            std::optional<uint64_t> expected_existing_content_id = std::nullopt,
            disk::utils::LogContext log_context = {}
        ) const -> drogon::Task<Result<ContentMetadata>>;

        [[nodiscard]]
        auto IncrementRefCount(
            const drogon::orm::DbClientPtr& client,
            uint64_t content_id,
            uint64_t increment = 1,
            disk::utils::LogContext log_context = {}
        ) const -> drogon::Task<Result<void>>;

        [[nodiscard]]
        auto IncrementRefCountsChecked(
            const drogon::orm::DbClientPtr& client,
            const std::unordered_map<uint64_t, uint64_t>& increments,
            const std::unordered_set<uint64_t>& existing_content_ids,
            disk::utils::LogContext log_context = {}
        ) const -> drogon::Task<Result<std::unordered_set<uint64_t>>>;

        [[nodiscard]]
        auto DecrementRefCountsAndEnqueueGc(
            const drogon::orm::DbClientPtr& client,
            const std::unordered_map<uint64_t, uint64_t>& decrements,
            disk::utils::LogContext log_context = {}
        ) const -> drogon::Task<Result<size_t>>;

    private:
        [[nodiscard]]
        auto CheckReferenceGate(
            const drogon::orm::DbClientPtr& client,
            uint64_t content_id,
            disk::utils::LogContext log_context
        ) const -> drogon::Task<Result<void>>;

        drogon::orm::DbClientPtr m_db_client;
    };

} // namespace disk::content
