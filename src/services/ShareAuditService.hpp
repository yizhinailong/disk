/**
 * @file ShareAuditService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 分享领域审计边界
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>
#include <json/json.h>

namespace disk::share {

    struct ShareAuditContext {
        std::string ip_address;
        std::string user_agent;
    };

    struct ShareCreateAuditEvent {
        uint64_t actor_user_id{ 0 };
        uint64_t share_id{ 0 };
        std::string share_code;
        std::vector<uint64_t> file_ids;
        std::vector<uint64_t> folder_ids;
        std::string permission;
        std::optional<std::string> expires_at;
        ShareAuditContext context;

        [[nodiscard]]
        auto ToDetails() const -> Json::Value;
    };

    struct ShareAccessAuditEvent {
        std::optional<uint64_t> share_id;
        std::string share_code;
        bool success{ false };
        std::string result;
        ShareAuditContext context;

        [[nodiscard]]
        auto ToDetails() const -> Json::Value;
    };

    struct SharePasswordFailureAuditEvent {
        std::optional<uint64_t> share_id;
        std::string share_code;
        uint64_t attempt_count{ 0 };
        bool counter_available{ false };
        bool rate_limited{ false };
        ShareAuditContext context;

        [[nodiscard]]
        auto ToDetails() const -> Json::Value;
    };

    struct ShareDownloadAuditEvent {
        uint64_t share_id{ 0 };
        std::string share_code;
        uint64_t file_id{ 0 };
        uint64_t bytes{ 0 };
        int http_status{ 0 };
        bool success{ false };
        std::string result;
        ShareAuditContext context;

        [[nodiscard]]
        auto ToDetails() const -> Json::Value;
    };

    struct ShareCancelAuditEvent {
        uint64_t actor_user_id{ 0 };
        std::optional<uint64_t> share_id;
        std::string share_code;
        bool success{ false };
        std::string result;
        ShareAuditContext context;

        [[nodiscard]]
        auto ToDetails() const -> Json::Value;
    };

    /**
     * @brief 分享领域唯一审计写入边界
     *
     * @details
     * 所有写入均 fail-open 且不自动重试。数据库错误会被记录，但不会改变分享业务结果。
     */
    class ShareAuditService {
    public:
        explicit ShareAuditService(drogon::orm::DbClientPtr db_client);

        auto RecordCreate(const ShareCreateAuditEvent& event) const -> drogon::Task<void>;
        auto RecordAccess(const ShareAccessAuditEvent& event) const -> drogon::Task<void>;
        auto RecordPasswordFailure(const SharePasswordFailureAuditEvent& event) const
            -> drogon::Task<void>;
        auto RecordDownload(const ShareDownloadAuditEvent& event) const -> drogon::Task<void>;
        auto RecordCancel(const ShareCancelAuditEvent& event) const -> drogon::Task<void>;

    private:
        auto record(
            const std::string& action,
            std::optional<uint64_t> actor_user_id,
            std::optional<uint64_t> share_id,
            const std::string& share_code,
            const Json::Value& details,
            const ShareAuditContext& context
        ) const -> drogon::Task<void>;

        drogon::orm::DbClientPtr m_db_client;
    };

} // namespace disk::share
