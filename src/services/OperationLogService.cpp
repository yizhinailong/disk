/**
 * @file OperationLogService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 操作日志服务
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "OperationLogService.hpp"

#include <drogon/orm/CoroMapper.h>

#include "models/OperationLogs.hpp"

namespace disk::log {

    OperationLogService::OperationLogService(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
        Logger::Debug(disk::utils::ServiceRuntimeLogContext()) << "Service initialized: service=operation_log";
    }

    auto OperationLogService::Log(
        const OperationLogEntry& entry,
        disk::utils::LogContext log_context
    ) -> drogon::Task<Result<void>> {
        try {
            drogon_model::disk::OperationLogs log;
            log.setUserId(entry.user_id);
            log.setAction(ActionTypeToString(entry.action));
            log.setTargetType(TargetTypeToString(entry.target_type));

            if (entry.target_id > 0) {
                log.setTargetId(entry.target_id);
            }
            if (!entry.target_name.empty()) {
                log.setTargetName(entry.target_name);
            }
            if (!entry.details.empty()) {
                log.setDetails(entry.details);
            }
            log.setIpAddress(NormalizeIpAddress(entry.ip_address));
            if (!entry.user_agent.empty()) {
                log.setUserAgent(entry.user_agent);
            }

            drogon::orm::CoroMapper<drogon_model::disk::OperationLogs> mapper(m_db_client);
            co_await mapper.insert(log);

            co_return {};
        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Error(log_context) << "Failed to record operation log";
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to record operation log")
            );
        }
    }

    auto OperationLogService::GetList(
        uint64_t user_id,
        int page,
        int page_size,
        disk::utils::LogContext log_context
    )
        -> drogon::Task<Result<OperationLogListResponse>> {

        OperationLogListResponse response;

        try {
            auto offset = (page - 1) * page_size;

            auto count_result = co_await m_db_client->execSqlCoro(
                "SELECT COUNT(*) as count FROM operation_logs WHERE user_id = $1",
                static_cast<int64_t>(user_id)
            );
            if (!count_result.empty()) {
                response.total = count_result[0]["count"].as<int>();
            }

            auto result = co_await m_db_client->execSqlCoro(
                "SELECT id, user_id, action, target_type, target_id, target_name, details, " "ip_address, created_at FROM operation_logs " "WHERE user_id = $1 ORDER BY created_at DESC LIMIT $2 OFFSET $3",
                static_cast<int64_t>(user_id),
                static_cast<int64_t>(page_size),
                static_cast<int64_t>(offset)
            );

            for (const auto& row : result) {
                OperationLogItem item;
                item.id = row["id"].as<uint64_t>();
                item.user_id = row["user_id"].as<uint64_t>();
                item.action = row["action"].as<std::string>();
                item.target_type = row["target_type"].as<std::string>();
                item.target_id = row["target_id"].isNull() ? 0 : row["target_id"].as<uint64_t>();
                item.target_name =
                    row["target_name"].isNull() ? "" : row["target_name"].as<std::string>();
                item.details = row["details"].isNull() ? "" : row["details"].as<std::string>();
                item.ip_address = row["ip_address"].as<std::string>();

                auto created_at = row["created_at"];
                if (!created_at.isNull()) {
                    item.created_at = created_at.as<std::string>();
                }

                response.items.push_back(item);
            }

            co_return response;
        } catch (const drogon::orm::DrogonDbException&) {
            Logger::Error(log_context) << "Failed to query operation logs";
            co_return std::unexpected(
                ErrorInfo(ErrorCode::InternalError, "Failed to query operation logs")
            );
        }
    }

    auto OperationLogService::ActionTypeToString(ActionType action) -> std::string {
        switch (action) {
            case ActionType::Login   : return "login";
            case ActionType::Logout  : return "logout";
            case ActionType::Upload  : return "upload";
            case ActionType::Download: return "download";
            case ActionType::Delete  : return "delete";
            case ActionType::Rename  : return "rename";
            case ActionType::Move    : return "move";
            case ActionType::Copy    : return "copy";
            case ActionType::Share   : return "share";
            case ActionType::Restore : return "restore";
            default                  : return "unknown";
        }
    }

    auto OperationLogService::TargetTypeToString(TargetType type) -> std::string {
        switch (type) {
            case TargetType::File  : return "file";
            case TargetType::Folder: return "folder";
            case TargetType::Share : return "share";
            case TargetType::User  : return "user";
            default                : return "unknown";
        }
    }

} // namespace disk::log
