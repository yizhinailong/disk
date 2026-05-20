/**
 * @file AdminDto.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 管理员模块数据传输对象（Data Transfer Objects）
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 本文件包含管理员模块的所有数据传输对象（DTO）：
 * - ListUsersRequest: 获取用户列表请求
 * - ChangeStatusRequest: 修改用户状态请求
 * - ChangeRoleRequest: 修改用户角色请求
 * - ListSharesRequest: 获取分享列表请求
 * - UserDetailResponse: 用户详情响应
 * - UserListResponse: 用户列表响应
 * - StorageStatsResponse: 存储统计响应
 * - SystemStatusResponse: 系统状态响应
 * - ShareListResponse: 分享列表响应
 * - ShareDetailResponse: 分享详情响应
 * - PaginationInfo: 分页信息
 */

#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <drogon/HttpRequest.h>
#include <json/json.h>

#include "utils/ErrorCode.hpp"

namespace disk::admin {

    // ==================== 分页结构 ====================

    /**
     * @brief 分页信息
     */
    struct PaginationInfo {
        int page{ 1 };
        int page_size{ 20 };
        int total{ 0 };
        int total_pages{ 0 };

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["page"] = page;
            json["page_size"] = page_size;
            json["total"] = total;
            json["total_pages"] = total_pages;
            return json;
        }
    };

    // ==================== Request DTOs ====================

    /**
     * @brief 获取用户列表请求 DTO
     *
     * @details
     * 验证规则：
     * - page: 默认 1，必须 >= 1
     * - page_size: 默认 20，必须 >= 1 且 <= 100
     * - username: 可选，筛选用户名
     * - email: 可选，筛选邮箱
     * - status: 可选，筛选状态（0/1/2）
     * - role: 可选，筛选角色（0/1）
     *
     * 从 URL 查询参数解析。
     */
    struct ListUsersRequest {
        int page{ 1 };
        int page_size{ 20 };
        std::optional<std::string> username;
        std::optional<std::string> email;
        std::optional<int> status;
        std::optional<int> role;

        /// 从 HTTP 请求查询参数解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<ListUsersRequest> {
            LOG_DEBUG << "Start parsing list users request parameters";

            ListUsersRequest request;

            // 解析可选参数 page
            auto page_str = req->getParameter("page");
            if (!page_str.empty()) {
                try {
                    size_t pos = 0;
                    auto value = std::stoi(page_str, &pos);
                    if (pos != page_str.length() || value < 1) {
                        LOG_WARN << "Parameter 'page' invalid value: " << page_str;
                        return std::unexpected(ErrorInfo(
                            ErrorCode::ValidationFailed,
                            "Parameter 'page' must be a positive integer"
                        ));
                    }
                    request.page = value;
                } catch (const std::exception& e) {
                    LOG_WARN << "Parameter 'page' invalid format: " << page_str;
                    return std::unexpected(
                        ErrorInfo(ErrorCode::ValidationFailed, "Parameter 'page' invalid format")
                    );
                }
            }

            // 解析可选参数 page_size
            auto page_size_str = req->getParameter("page_size");
            if (!page_size_str.empty()) {
                try {
                    size_t pos = 0;
                    auto value = std::stoi(page_size_str, &pos);
                    if (pos != page_size_str.length() || value < 1 || value > 100) {
                        LOG_WARN << "Parameter 'page_size' invalid value: " << page_size_str;
                        return std::unexpected(ErrorInfo(
                            ErrorCode::ValidationFailed,
                            "Parameter 'page_size' must be an integer between 1-100"
                        ));
                    }
                    request.page_size = value;
                } catch (const std::exception& e) {
                    LOG_WARN << "Parameter 'page_size' invalid format: " << page_size_str;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'page_size' invalid format"
                    ));
                }
            }

            // 解析可选参数 username
            auto username_str = req->getParameter("username");
            if (!username_str.empty()) {
                request.username = username_str;
            }

            // 解析可选参数 email
            auto email_str = req->getParameter("email");
            if (!email_str.empty()) {
                request.email = email_str;
            }

            // 解析可选参数 status
            auto status_str = req->getParameter("status");
            if (!status_str.empty()) {
                try {
                    size_t pos = 0;
                    auto value = std::stoi(status_str, &pos);
                    if (pos != status_str.length()) {
                        LOG_WARN << "Parameter 'status' invalid format: " << status_str;
                        return std::unexpected(ErrorInfo(
                            ErrorCode::ValidationFailed,
                            "Parameter 'status' invalid format"
                        ));
                    }
                    if (value < 0 || value > 2) {
                        LOG_WARN << "Parameter 'status' invalid value: " << status_str;
                        return std::unexpected(
                            ErrorInfo(ErrorCode::AdminInvalidStatus, "Invalid status value")
                        );
                    }
                    request.status = value;
                } catch (const std::exception& e) {
                    LOG_WARN << "Parameter 'status' invalid format: " << status_str;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'status' invalid format"
                    ));
                }
            }

            // 解析可选参数 role
            auto role_str = req->getParameter("role");
            if (!role_str.empty()) {
                try {
                    size_t pos = 0;
                    auto value = std::stoi(role_str, &pos);
                    if (pos != role_str.length()) {
                        LOG_WARN << "Parameter 'role' invalid format: " << role_str;
                        return std::unexpected(ErrorInfo(
                            ErrorCode::ValidationFailed,
                            "Parameter 'role' invalid format"
                        ));
                    }
                    if (value < 0 || value > 1) {
                        LOG_WARN << "Parameter 'role' invalid value: " << role_str;
                        return std::unexpected(
                            ErrorInfo(ErrorCode::AdminInvalidRole, "Invalid role value")
                        );
                    }
                    request.role = value;
                } catch (const std::exception& e) {
                    LOG_WARN << "Parameter 'role' invalid format: " << role_str;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'role' invalid format"
                    ));
                }
            }

            LOG_DEBUG << "Parsed list users request: page=" << request.page
                      << ", page_size=" << request.page_size
                      << ", username=" << (request.username.has_value() ? "set" : "null")
                      << ", email=" << (request.email.has_value() ? "set" : "null")
                      << ", status=" << (request.status.has_value() ? std::to_string(*request.status) : "null")
                      << ", role=" << (request.role.has_value() ? std::to_string(*request.role) : "null");

            return request;
        }
    };

    /**
     * @brief 修改用户状态请求 DTO
     *
     * @details
     * 验证规则：
     * - status: 必填，0/1/2（禁用/正常/锁定）
     *
     * 注意：user_id 从 URL 路径参数获取，不在本 DTO 中
     */
    struct ChangeStatusRequest {
        int status{ 0 };

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<ChangeStatusRequest> {
            LOG_DEBUG << "Start parsing change status request parameters";

            auto json_ptr = req->getJsonObject();
            if (!json_ptr) {
                LOG_WARN << "Request body is not valid JSON";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Request body is not valid JSON")
                );
            }

            const auto& json = *json_ptr;

            if (!json.isMember("status")) {
                LOG_WARN << "Missing required parameter: status";
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "Missing required parameter: status"
                ));
            }

            if (!json["status"].isIntegral()) {
                LOG_WARN << "Parameter 'status' type error: expected integer";
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "Parameter 'status' type error: expected integer"
                ));
            }

            auto status = json["status"].asInt();
            if (status < 0 || status > 2) {
                LOG_WARN << "Parameter 'status' invalid value: " << status;
                return std::unexpected(
                    ErrorInfo(ErrorCode::AdminInvalidStatus, "Invalid status value")
                );
            }

            ChangeStatusRequest request;
            request.status = status;

            LOG_DEBUG << "Parsed change status request: status=" << request.status;

            return request;
        }
    };

    /**
     * @brief 修改用户角色请求 DTO
     *
     * @details
     * 验证规则：
     * - role: 必填，0/1（普通用户/管理员）
     *
     * 注意：user_id 从 URL 路径参数获取，不在本 DTO 中
     */
    struct ChangeRoleRequest {
        int role{ 0 };

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<ChangeRoleRequest> {
            LOG_DEBUG << "Start parsing change role request parameters";

            auto json_ptr = req->getJsonObject();
            if (!json_ptr) {
                LOG_WARN << "Request body is not valid JSON";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Request body is not valid JSON")
                );
            }

            const auto& json = *json_ptr;

            if (!json.isMember("role")) {
                LOG_WARN << "Missing required parameter: role";
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "Missing required parameter: role"
                ));
            }

            if (!json["role"].isIntegral()) {
                LOG_WARN << "Parameter 'role' type error: expected integer";
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "Parameter 'role' type error: expected integer"
                ));
            }

            auto role = json["role"].asInt();
            if (role < 0 || role > 1) {
                LOG_WARN << "Parameter 'role' invalid value: " << role;
                return std::unexpected(
                    ErrorInfo(ErrorCode::AdminInvalidRole, "Invalid role value")
                );
            }

            ChangeRoleRequest request;
            request.role = role;

            LOG_DEBUG << "Parsed change role request: role=" << request.role;

            return request;
        }
    };

    /**
     * @brief 获取分享列表请求 DTO
     *
     * @details
     * 验证规则：
     * - page: 默认 1，必须 >= 1
     * - page_size: 默认 20，必须 >= 1 且 <= 100
     * - status: 可选，筛选状态（0/1/2）
     * - user_id: 可选，筛选用户 ID
     * - username: 可选，按分享者用户名模糊筛选
     *
     * 从 URL 查询参数解析。
     */
    struct ListSharesRequest {
        int page{ 1 };
        int page_size{ 20 };
        std::optional<int> status;
        std::optional<uint64_t> user_id;
        std::optional<std::string> username;

        /// 从 HTTP 请求查询参数解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<ListSharesRequest> {
            LOG_DEBUG << "Start parsing list shares request parameters";

            ListSharesRequest request;

            // 解析可选参数 page
            auto page_str = req->getParameter("page");
            if (!page_str.empty()) {
                try {
                    size_t pos = 0;
                    auto value = std::stoi(page_str, &pos);
                    if (pos != page_str.length() || value < 1) {
                        LOG_WARN << "Parameter 'page' invalid value: " << page_str;
                        return std::unexpected(ErrorInfo(
                            ErrorCode::ValidationFailed,
                            "Parameter 'page' must be a positive integer"
                        ));
                    }
                    request.page = value;
                } catch (const std::exception& e) {
                    LOG_WARN << "Parameter 'page' invalid format: " << page_str;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'page' invalid format"
                    ));
                }
            }

            // 解析可选参数 page_size
            auto page_size_str = req->getParameter("page_size");
            if (!page_size_str.empty()) {
                try {
                    size_t pos = 0;
                    auto value = std::stoi(page_size_str, &pos);
                    if (pos != page_size_str.length() || value < 1 || value > 100) {
                        LOG_WARN << "Parameter 'page_size' invalid value: " << page_size_str;
                        return std::unexpected(ErrorInfo(
                            ErrorCode::ValidationFailed,
                            "Parameter 'page_size' must be an integer between 1-100"
                        ));
                    }
                    request.page_size = value;
                } catch (const std::exception& e) {
                    LOG_WARN << "Parameter 'page_size' invalid format: " << page_size_str;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'page_size' invalid format"
                    ));
                }
            }

            // 解析可选参数 status
            auto status_str = req->getParameter("status");
            if (!status_str.empty()) {
                try {
                    size_t pos = 0;
                    auto value = std::stoi(status_str, &pos);
                    if (pos != status_str.length()) {
                        LOG_WARN << "Parameter 'status' invalid format: " << status_str;
                        return std::unexpected(ErrorInfo(
                            ErrorCode::ValidationFailed,
                            "Parameter 'status' invalid format"
                        ));
                    }
                    if (value < 0 || value > 2) {
                        LOG_WARN << "Parameter 'status' invalid value: " << status_str;
                        return std::unexpected(
                            ErrorInfo(ErrorCode::AdminInvalidStatus, "Invalid status value")
                        );
                    }
                    request.status = value;
                } catch (const std::exception& e) {
                    LOG_WARN << "Parameter 'status' invalid format: " << status_str;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'status' invalid format"
                    ));
                }
            }

            // 解析可选参数 user_id
            auto user_id_str = req->getParameter("user_id");
            if (!user_id_str.empty()) {
                try {
                    size_t pos = 0;
                    auto value = std::stoull(user_id_str, &pos);
                    if (pos != user_id_str.length()) {
                        LOG_WARN << "Parameter 'user_id' invalid format: " << user_id_str;
                        return std::unexpected(ErrorInfo(
                            ErrorCode::ValidationFailed,
                            "Parameter 'user_id' invalid format"
                        ));
                    }
                    request.user_id = value;
                } catch (const std::exception& e) {
                    LOG_WARN << "Parameter 'user_id' invalid format: " << user_id_str;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'user_id' invalid format"
                    ));
                }
            }

            // 解析可选参数 username
            auto username = req->getParameter("username");
            if (!username.empty()) {
                request.username = username;
            }

            LOG_DEBUG << "Parsed list shares request: page=" << request.page
                      << ", page_size=" << request.page_size
                      << ", status=" << (request.status.has_value() ? std::to_string(*request.status) : "null")
                      << ", user_id=" << (request.user_id.has_value() ? std::to_string(*request.user_id) : "null")
                      << ", username=" << (request.username.has_value() ? *request.username : "null");

            return request;
        }
    };

    // ==================== Response DTOs ====================

    /**
     * @brief 用户详情响应 DTO
     *
     * @details
     * 包含用户的完整信息，用于管理员查看用户详情。
     */
    struct UserDetailResponse {
        uint64_t id;
        std::string username;
        std::string email;
        std::string nickname;
        std::string avatar;
        int role{ 0 };
        int status{ 0 };
        uint64_t storage_quota{ 0 };
        uint64_t storage_used{ 0 };
        uint64_t storage_reserved{ 0 };
        std::string created_at;
        std::string last_login_at;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["id"] = static_cast<Json::UInt64>(id);
            json["username"] = username;
            json["email"] = email;
            json["nickname"] = nickname;
            json["avatar"] = avatar;
            json["role"] = role;
            json["status"] = status;
            json["storage_quota"] = static_cast<Json::UInt64>(storage_quota);
            json["storage_used"] = static_cast<Json::UInt64>(storage_used);
            json["storage_reserved"] = static_cast<Json::UInt64>(storage_reserved);
            json["created_at"] = created_at;
            json["last_login_at"] = last_login_at;
            return json;
        }
    };

    /**
     * @brief 用户列表响应 DTO
     *
     * @details
     * 包含用户列表和分页信息。
     */
    struct UserListResponse {
        std::vector<UserDetailResponse> items;
        PaginationInfo pagination;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            Json::Value items_array(Json::arrayValue);
            for (const auto& item : items) {
                items_array.append(item.ToJson());
            }
            json["items"] = items_array;
            json["pagination"] = pagination.ToJson();
            return json;
        }
    };

    /**
     * @brief 存储统计响应 DTO
     *
     * @details
     * 包含系统整体存储统计信息。
     */
    struct StorageStatsResponse {
        int total_users{ 0 };
        int total_files{ 0 };
        uint64_t total_storage_used{ 0 };
        uint64_t total_storage_quota{ 0 };
        int active_shares{ 0 };

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["total_users"] = total_users;
            json["total_files"] = total_files;
            json["total_storage_used"] = static_cast<Json::UInt64>(total_storage_used);
            json["total_storage_quota"] = static_cast<Json::UInt64>(total_storage_quota);
            json["active_shares"] = active_shares;
            return json;
        }
    };

    /**
     * @brief 系统状态响应 DTO
     *
     * @details
     * 包含系统运行状态和资源使用情况。
     */
    struct SystemStatusResponse {
        bool mysql_connected{ false };
        bool redis_connected{ false };
        uint64_t disk_total{ 0 };
        uint64_t disk_used{ 0 };
        uint64_t disk_free{ 0 };
        uint64_t uptime_seconds{ 0 };

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["mysql_connected"] = mysql_connected;
            json["redis_connected"] = redis_connected;
            json["disk_total"] = static_cast<Json::UInt64>(disk_total);
            json["disk_used"] = static_cast<Json::UInt64>(disk_used);
            json["disk_free"] = static_cast<Json::UInt64>(disk_free);
            json["uptime_seconds"] = static_cast<Json::UInt64>(uptime_seconds);
            return json;
        }
    };

    /**
     * @brief 分享详情响应 DTO
     *
     * @details
     * 包含分享的详细信息，用于管理员查看分享详情。
     */
    struct ShareDetailResponse {
        uint64_t id;
        uint64_t user_id;
        std::string username;
        uint64_t file_id;
        std::string file_name;
        std::string share_code;
        int status{ 0 };
        int access_count{ 0 };
        bool password_set{ false };
        std::string created_at;
        std::string expires_at;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["id"] = static_cast<Json::UInt64>(id);
            json["user_id"] = static_cast<Json::UInt64>(user_id);
            json["username"] = username;
            json["file_id"] = static_cast<Json::UInt64>(file_id);
            json["file_name"] = file_name;
            json["share_code"] = share_code;
            json["status"] = status;
            json["access_count"] = access_count;
            json["password_set"] = password_set;
            json["created_at"] = created_at;
            json["expires_at"] = expires_at;
            return json;
        }
    };

    /**
     * @brief 分享列表响应 DTO
     *
     * @details
     * 包含分享列表和分页信息。
     */
    struct ShareListResponse {
        std::vector<ShareDetailResponse> items;
        PaginationInfo pagination;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            Json::Value items_array(Json::arrayValue);
            for (const auto& item : items) {
                items_array.append(item.ToJson());
            }
            json["items"] = items_array;
            json["pagination"] = pagination.ToJson();
            return json;
        }
    };

} // namespace disk::admin