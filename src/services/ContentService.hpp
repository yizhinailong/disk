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
        int ref_count{ 1 };
    };

    struct ZeroRefContent {
        uint64_t id{ 0 };
        std::string storage_path;
    };

    /**
     * @brief 文件内容领域服务
     *
     * 集中封装 file_contents 的查找、创建、引用计数更新与零引用验证。
     * 物理 blob 删除仍由调用方在显式安全检查后执行。
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
        auto FindByMd5(const std::string& hash_md5) const
            -> drogon::Task<std::optional<ContentMetadata>>;

        [[nodiscard]]
        auto FindByMd5(const drogon::orm::DbClientPtr& client, const std::string& hash_md5) const
            -> drogon::Task<std::optional<ContentMetadata>>;

        [[nodiscard]]
        auto FindExistingIds(
            const drogon::orm::DbClientPtr& client,
            const std::vector<uint64_t>& content_ids
        ) const -> drogon::Task<std::unordered_set<uint64_t>>;

        [[nodiscard]]
        auto Create(const drogon::orm::DbClientPtr& client, const NewContent& content) const
            -> drogon::Task<ContentMetadata>;

        [[nodiscard]]
        auto IncrementRefCount(
            const drogon::orm::DbClientPtr& client,
            uint64_t content_id,
            uint64_t increment = 1
        ) const -> drogon::Task<Result<void>>;

        [[nodiscard]]
        auto IncrementRefCounts(
            const drogon::orm::DbClientPtr& client,
            const std::unordered_map<uint64_t, uint64_t>& increments,
            const std::unordered_set<uint64_t>& existing_content_ids
        ) const -> drogon::Task<std::unordered_set<uint64_t>>;

        [[nodiscard]]
        auto IncrementRefCountsChecked(
            const drogon::orm::DbClientPtr& client,
            const std::unordered_map<uint64_t, uint64_t>& increments,
            const std::unordered_set<uint64_t>& existing_content_ids
        ) const -> drogon::Task<Result<std::unordered_set<uint64_t>>>;

        [[nodiscard]]
        auto DecrementRefCounts(
            const drogon::orm::DbClientPtr& client,
            const std::unordered_map<uint64_t, uint64_t>& decrements
        ) const -> drogon::Task<std::vector<ZeroRefContent>>;

        [[nodiscard]]
        auto VerifyZeroRefContents(
            const drogon::orm::DbClientPtr& client,
            const std::vector<uint64_t>& content_ids
        ) const -> drogon::Task<std::vector<ZeroRefContent>>;

    private:
        drogon::orm::DbClientPtr m_db_client;
    };

} ///< namespace disk::content
