/**
 * @file ShareAuditService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 分享领域审计边界实现
 *
 * @copyright Copyright (c) 2026
 */

#include "services/ShareAuditService.hpp"

#include <algorithm>
#include <exception>
#include <utility>

#include <drogon/orm/Exception.h>
#include <drogon/orm/SqlBinder.h>
#include <json/writer.h>

#include "utils/LogHelper.hpp"

namespace disk::share {

    namespace {
        constexpr std::size_t MAX_IP_ADDRESS_LENGTH = 45;
        constexpr std::size_t MAX_USER_AGENT_LENGTH = 512;

        [[nodiscard]] auto SerializeDetails(const Json::Value& details) -> std::string {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            return Json::writeString(builder, details);
        }

        [[nodiscard]] auto Truncate(const std::string& value, std::size_t max_length)
            -> std::string {
            return value.substr(0, std::min(value.size(), max_length));
        }

        auto SetIds(Json::Value& details, const char* key, const std::vector<uint64_t>& ids)
            -> void {
            details[key] = Json::Value(Json::arrayValue);
            for (const auto id : ids) {
                details[key].append(static_cast<Json::UInt64>(id));
            }
        }
    } // namespace

    auto ShareCreateAuditEvent::ToDetails() const -> Json::Value {
        Json::Value details;
        details["share_code"] = share_code;
        SetIds(details, "file_ids", file_ids);
        SetIds(details, "folder_ids", folder_ids);
        details["permission"] = permission;
        if (expires_at.has_value()) {
            details["expires_at"] = *expires_at;
        } else {
            details["expires_at"] = Json::Value(Json::nullValue);
        }
        details["success"] = true;
        details["result"] = "success";
        return details;
    }

    auto ShareAccessAuditEvent::ToDetails() const -> Json::Value {
        Json::Value details;
        details["share_code"] = share_code;
        details["success"] = success;
        details["result"] = result;
        return details;
    }

    auto SharePasswordFailureAuditEvent::ToDetails() const -> Json::Value {
        Json::Value details;
        details["share_code"] = share_code;
        details["attempt_count"] = static_cast<Json::UInt64>(attempt_count);
        details["counter_available"] = counter_available;
        details["rate_limited"] = rate_limited;
        details["success"] = false;
        details["result"] = rate_limited ? "rate_limited" : "validation_failed";
        return details;
    }

    auto ShareDownloadAuditEvent::ToDetails() const -> Json::Value {
        Json::Value details;
        details["share_code"] = share_code;
        details["file_id"] = static_cast<Json::UInt64>(file_id);
        details["bytes"] = static_cast<Json::UInt64>(bytes);
        details["http_status"] = http_status;
        details["success"] = success;
        details["result"] = result;
        details["request_id"] =
            log_context.request_id.has_value() && !log_context.request_id->empty() ?
                Json::Value(*log_context.request_id) :
                Json::Value(Json::nullValue);
        details["operation"] =
            log_context.operation.has_value() && !log_context.operation->empty() ?
                Json::Value(*log_context.operation) :
                Json::Value(Json::nullValue);
        return details;
    }

    auto ShareCancelAuditEvent::ToDetails() const -> Json::Value {
        Json::Value details;
        details["share_code"] = share_code;
        details["cancelled_by"] = static_cast<Json::UInt64>(actor_user_id);
        details["success"] = success;
        details["result"] = result;
        return details;
    }

    ShareAuditService::ShareAuditService(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
    }

    auto ShareAuditService::RecordCreate(const ShareCreateAuditEvent& event) const
        -> drogon::Task<void> {
        co_await record(
            "share_create",
            event.actor_user_id,
            event.share_id,
            event.share_code,
            event.ToDetails(),
            event.context,
            {}
        );
    }

    auto ShareAuditService::RecordAccess(const ShareAccessAuditEvent& event) const
        -> drogon::Task<void> {
        co_await record(
            "share_access",
            std::nullopt,
            event.share_id,
            event.share_code,
            event.ToDetails(),
            event.context,
            {}
        );
    }

    auto ShareAuditService::RecordPasswordFailure(
        const SharePasswordFailureAuditEvent& event
    ) const -> drogon::Task<void> {
        co_await record(
            "share_pwd_fail",
            std::nullopt,
            event.share_id,
            event.share_code,
            event.ToDetails(),
            event.context,
            {}
        );
    }

    auto ShareAuditService::RecordDownload(const ShareDownloadAuditEvent& event) const
        -> drogon::Task<void> {
        co_await record(
            "share_download",
            std::nullopt,
            event.share_id,
            event.share_code,
            event.ToDetails(),
            event.context,
            event.log_context
        );
    }

    auto ShareAuditService::RecordCancel(const ShareCancelAuditEvent& event) const
        -> drogon::Task<void> {
        co_await record(
            "share_cancel",
            event.actor_user_id,
            event.share_id,
            event.share_code,
            event.ToDetails(),
            event.context,
            {}
        );
    }

    auto ShareAuditService::record(
        const std::string& action,
        std::optional<uint64_t> actor_user_id,
        std::optional<uint64_t> share_id,
        const std::string& share_code,
        const Json::Value& details,
        const ShareAuditContext& context,
        disk::utils::LogContext log_context
    ) const -> drogon::Task<void> {
        try {
            auto binder = *m_db_client
                       << "INSERT INTO operation_logs " "(user_id, action, target_type, target_id, target_name, details, " "ip_address, user_agent) " "VALUES ($1, $2, 'share', $3, $4, $5::jsonb, $6, $7)";

            if (actor_user_id.has_value()) {
                binder << static_cast<int64_t>(*actor_user_id);
            } else {
                binder << nullptr;
            }
            binder << action;
            if (share_id.has_value()) {
                binder << static_cast<int64_t>(*share_id);
            } else {
                binder << nullptr;
            }

            const auto ip_address = context.ip_address.empty() ? std::string("unknown") : Truncate(context.ip_address, MAX_IP_ADDRESS_LENGTH);
            binder << share_code << SerializeDetails(details) << ip_address
                   << Truncate(context.user_agent, MAX_USER_AGENT_LENGTH);

            co_await drogon::orm::internal::SqlAwaiter(std::move(binder));
        } catch (const drogon::orm::DrogonDbException& error) {
            Logger::Error(log_context)
                << "Failed to record share audit event: action=" << action
                << ", share_id="
                << (share_id.has_value() ? std::to_string(*share_id) : "null")
                << ", share_code=" << share_code
                << ", error=" << error.base().what();
        } catch (const std::exception& error) {
            Logger::Error(log_context)
                << "Failed to record share audit event: action=" << action
                << ", share_id="
                << (share_id.has_value() ? std::to_string(*share_id) : "null")
                << ", share_code=" << share_code << ", error=" << error.what();
        }
    }

} // namespace disk::share
