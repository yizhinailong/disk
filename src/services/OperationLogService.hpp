/**
 * @file OperationLogService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 操作日志服务
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <string>

#include <drogon/orm/DbClient.h>
#include <json/json.h>

#include "utils/ErrorCode.hpp"

namespace disk::log {

    enum class ActionType : uint8_t {
        Login,
        Logout,
        Upload,
        Download,
        Delete,
        Rename,
        Move,
        Copy,
        Share,
        Restore,
    };

    enum class TargetType : uint8_t {
        File,
        Folder,
        Share,
        User,
    };

    struct OperationLogEntry {
        uint64_t user_id{ 0 };
        ActionType action;
        TargetType target_type;
        uint64_t target_id{ 0 };
        std::string target_name;
        std::string details;
        std::string ip_address;
        std::string user_agent;
    };

    struct OperationLogItem {
        uint64_t id{ 0 };
        uint64_t user_id{ 0 };
        std::string action;
        std::string target_type;
        uint64_t target_id{ 0 };
        std::string target_name;
        std::string details;
        std::string ip_address;
        std::string created_at;

        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["id"] = static_cast<Json::UInt64>(id);
            json["action"] = action;
            json["target_type"] = target_type;
            json["target_id"] = static_cast<Json::UInt64>(target_id);
            json["target_name"] = target_name;
            json["details"] = details;
            json["ip_address"] = ip_address;
            json["created_at"] = created_at;
            return json;
        }
    };

    struct OperationLogListResponse {
        std::vector<OperationLogItem> items;
        int total{ 0 };
    };

    class OperationLogService {
    public:
        explicit OperationLogService(drogon::orm::DbClientPtr db_client);

        [[nodiscard]]
        auto Log(const OperationLogEntry& entry) -> drogon::Task<Result<void>>;

        [[nodiscard]]
        auto GetList(uint64_t user_id, int page, int page_size)
            -> drogon::Task<Result<OperationLogListResponse>>;

        [[nodiscard]]
        static auto NormalizeIpAddress(const std::string& ip) -> std::string {
            return ip.empty() ? "unknown" : ip;
        }

    private:
        static auto ActionTypeToString(ActionType action) -> std::string;
        static auto TargetTypeToString(TargetType type) -> std::string;

        drogon::orm::DbClientPtr m_db_client;
    };

} ///< namespace disk::log
