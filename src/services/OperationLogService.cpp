/**
 * @file OperationLogService.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 操作日志服务
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "OperationLogService.hpp"

namespace disk::log {

    OperationLogService::OperationLogService(drogon::orm::DbClientPtr db_client)
        : m_db_client(std::move(db_client)) {
        Logger::Debug(disk::utils::ServiceRuntimeLogContext()) << "Service initialized: service=operation_log";
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
                "SELECT id, action, target_type, target_id, target_name, details, " "ip_address, created_at FROM operation_logs " "WHERE user_id = $1 ORDER BY created_at DESC LIMIT $2 OFFSET $3",
                static_cast<int64_t>(user_id),
                static_cast<int64_t>(page_size),
                static_cast<int64_t>(offset)
            );

            for (const auto& row : result) {
                OperationLogItem item;
                item.id = row["id"].as<uint64_t>();
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

} // namespace disk::log
