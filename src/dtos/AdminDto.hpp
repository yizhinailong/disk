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
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <drogon/HttpRequest.h>
#include <json/json.h>

#include "utils/DtoBase.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::admin {

    /// ==================== 分页结构 ====================

    /**
     * @brief 分页信息
     */
    struct PaginationInfo : DtoBase<PaginationInfo> {
        int page{ 1 };
        int page_size{ 20 };
        int total{ 0 };
        int total_pages{ 0 };

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "page", page);
            SetField(json, "page_size", page_size);
            SetField(json, "total", total);
            SetField(json, "total_pages", total_pages);
            return json;
        }
    };

    /// ==================== Request DTOs ====================

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
    struct ListUsersRequest : DtoBase<ListUsersRequest> {
        int page{ 1 };
        int page_size{ 20 };
        std::optional<std::string> username;
        std::optional<std::string> email;
        std::optional<int> status;
        std::optional<int> role;

        /// 从 HTTP 请求查询参数解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<ListUsersRequest> {
            Logger::Debug() << "Start parsing list users request parameters";

            ListUsersRequest request;

            /// 解析可选参数 page
            auto page_result = QueryPositiveInt(req, "page", 1);
            if (!page_result) return std::unexpected(page_result.error());
            if (page_result->has_value()) {
                request.page = **page_result;
            }

            /// 解析可选参数 page_size
            auto page_size_result = QueryPositiveInt(req, "page_size", 1, 100);
            if (!page_size_result) return std::unexpected(page_size_result.error());
            if (page_size_result->has_value()) {
                request.page_size = **page_size_result;
            }

            /// 解析可选参数 username
            auto username_str = req->getParameter("username");
            if (!username_str.empty()) {
                request.username = username_str;
            }

            /// 解析可选参数 email
            auto email_str = req->getParameter("email");
            if (!email_str.empty()) {
                request.email = email_str;
            }

            /// 解析可选参数 status
            auto status_str = req->getParameter("status");
            if (!status_str.empty()) {
                try {
                    size_t pos = 0;
                    auto value = std::stoi(status_str, &pos);
                    if (pos != status_str.length()) {
                        Logger::Warn() << "Parameter 'status' invalid format: " << status_str;
                        return std::unexpected(ErrorInfo(
                            ErrorCode::ValidationFailed,
                            "Parameter 'status' invalid format"
                        ));
                    }
                    if (value < 0 || value > 2) {
                        Logger::Warn() << "Parameter 'status' invalid value: " << status_str;
                        return std::unexpected(
                            ErrorInfo(ErrorCode::AdminInvalidStatus, "Invalid status value")
                        );
                    }
                    request.status = value;
                } catch (const std::exception& e) {
                    Logger::Warn() << "Parameter 'status' invalid format: " << status_str;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'status' invalid format"
                    ));
                }
            }

            /// 解析可选参数 role
            auto role_str = req->getParameter("role");
            if (!role_str.empty()) {
                try {
                    size_t pos = 0;
                    auto value = std::stoi(role_str, &pos);
                    if (pos != role_str.length()) {
                        Logger::Warn() << "Parameter 'role' invalid format: " << role_str;
                        return std::unexpected(ErrorInfo(
                            ErrorCode::ValidationFailed,
                            "Parameter 'role' invalid format"
                        ));
                    }
                    if (value < 0 || value > 1) {
                        Logger::Warn() << "Parameter 'role' invalid value: " << role_str;
                        return std::unexpected(
                            ErrorInfo(ErrorCode::AdminInvalidRole, "Invalid role value")
                        );
                    }
                    request.role = value;
                } catch (const std::exception& e) {
                    Logger::Warn() << "Parameter 'role' invalid format: " << role_str;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'role' invalid format"
                    ));
                }
            }

            Logger::Debug() << "Parsed list users request: page=" << request.page
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
    struct ChangeStatusRequest : DtoBase<ChangeStatusRequest> {
        int status{ 0 };

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<ChangeStatusRequest> {
            Logger::Debug() << "Start parsing change status request parameters";

            auto json_result = RequireJsonBody(req);
            if (!json_result) return std::unexpected(json_result.error());
            const auto& json = *json_result.value();

            auto status_result = RequireInt(json, "status");
            if (!status_result) return std::unexpected(status_result.error());

            if (*status_result < 0 || *status_result > 2) {
                Logger::Warn() << "Parameter 'status' invalid value: " << *status_result;
                return std::unexpected(
                    ErrorInfo(ErrorCode::AdminInvalidStatus, "Invalid status value")
                );
            }

            ChangeStatusRequest request;
            request.status = *status_result;

            Logger::Debug() << "Parsed change status request: status=" << request.status;

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
    struct ChangeRoleRequest : DtoBase<ChangeRoleRequest> {
        int role{ 0 };

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<ChangeRoleRequest> {
            Logger::Debug() << "Start parsing change role request parameters";

            auto json_result = RequireJsonBody(req);
            if (!json_result) return std::unexpected(json_result.error());
            const auto& json = *json_result.value();

            auto role_result = RequireInt(json, "role");
            if (!role_result) return std::unexpected(role_result.error());

            if (*role_result < 0 || *role_result > 1) {
                Logger::Warn() << "Parameter 'role' invalid value: " << *role_result;
                return std::unexpected(
                    ErrorInfo(ErrorCode::AdminInvalidRole, "Invalid role value")
                );
            }

            ChangeRoleRequest request;
            request.role = *role_result;

            Logger::Debug() << "Parsed change role request: role=" << request.role;

            return request;
        }
    };

    /**
     * @brief 修改用户可用空间请求 DTO
     *
     * @details
     * 验证规则：
     * - available_space_g: 必填，非负整数，单位 G
     *
     * 注意：user_id 从 URL 路径参数获取，不在本 DTO 中
     */
    struct ChangeAvailableSpaceRequest : DtoBase<ChangeAvailableSpaceRequest> {
        static constexpr uint64_t BytesPerG = 1024ULL * 1024ULL * 1024ULL;

        uint64_t available_space_g{ 0 };

        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<ChangeAvailableSpaceRequest> {
            Logger::Debug() << "Start parsing change available space request parameters";

            auto json_result = RequireJsonBody(req);
            if (!json_result) return std::unexpected(json_result.error());
            const auto& json = *json_result.value();

            auto space_result = RequireUInt64(json, "available_space_g");
            if (!space_result) return std::unexpected(space_result.error());

            if (*space_result > std::numeric_limits<uint64_t>::max() / BytesPerG) {
                Logger::Warn() << "Parameter 'available_space_g' is too large: " << *space_result;
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "Parameter 'available_space_g' is too large"
                ));
            }

            ChangeAvailableSpaceRequest request;
            request.available_space_g = *space_result;

            Logger::Debug() << "Parsed change available space request: available_space_g="
                      << request.available_space_g;

            return request;
        }
    };


    struct ListSharesRequest : DtoBase<ListSharesRequest> {
        int page{ 1 };
        int page_size{ 20 };
        std::optional<int> status;
        std::optional<uint64_t> user_id;
        std::optional<std::string> username;

        /// 从 HTTP 请求查询参数解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<ListSharesRequest> {
            Logger::Debug() << "Start parsing list shares request parameters";

            ListSharesRequest request;

            /// 解析可选参数 page
            auto page_result = QueryPositiveInt(req, "page", 1);
            if (!page_result) return std::unexpected(page_result.error());
            if (page_result->has_value()) {
                request.page = **page_result;
            }

            /// 解析可选参数 page_size
            auto page_size_result = QueryPositiveInt(req, "page_size", 1, 100);
            if (!page_size_result) return std::unexpected(page_size_result.error());
            if (page_size_result->has_value()) {
                request.page_size = **page_size_result;
            }

            /// 解析可选参数 status
            auto status_str = req->getParameter("status");
            if (!status_str.empty()) {
                try {
                    size_t pos = 0;
                    auto value = std::stoi(status_str, &pos);
                    if (pos != status_str.length()) {
                        Logger::Warn() << "Parameter 'status' invalid format: " << status_str;
                        return std::unexpected(ErrorInfo(
                            ErrorCode::ValidationFailed,
                            "Parameter 'status' invalid format"
                        ));
                    }
                    if (value < 0 || value > 2) {
                        Logger::Warn() << "Parameter 'status' invalid value: " << status_str;
                        return std::unexpected(
                            ErrorInfo(ErrorCode::AdminInvalidStatus, "Invalid status value")
                        );
                    }
                    request.status = value;
                } catch (const std::exception& e) {
                    Logger::Warn() << "Parameter 'status' invalid format: " << status_str;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'status' invalid format"
                    ));
                }
            }

            /// 解析可选参数 user_id
            auto user_id_result = QueryUInt64(req, "user_id");
            if (!user_id_result) return std::unexpected(user_id_result.error());
            request.user_id = *user_id_result;

            /// 解析可选参数 username
            auto username = req->getParameter("username");
            if (!username.empty()) {
                request.username = username;
            }

            Logger::Debug() << "Parsed list shares request: page=" << request.page
                      << ", page_size=" << request.page_size
                      << ", status=" << (request.status.has_value() ? std::to_string(*request.status) : "null")
                      << ", user_id=" << (request.user_id.has_value() ? std::to_string(*request.user_id) : "null")
                      << ", username=" << (request.username.has_value() ? *request.username : "null");

            return request;
        }
    };

    /**
     * @brief 获取操作日志列表请求 DTO
     *
     * @details
     * 验证规则：
     * - page: 默认 1，必须 >= 1
     * - page_size: 默认 20，必须 >= 1 且 <= 100
     * - action: 可选，筛选操作类型
     * - start_date: 可选，筛选开始日期（YYYY-MM-DD）
     * - end_date: 可选，筛选结束日期（YYYY-MM-DD）
     *
     * 从 URL 查询参数解析。
     */
    struct AdminLogListRequest : DtoBase<AdminLogListRequest> {
        int page{ 1 };
        int page_size{ 20 };
        std::optional<std::string> action;
        std::optional<std::string> start_date;
        std::optional<std::string> end_date;

        /// 从 HTTP 请求查询参数解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<AdminLogListRequest> {
            Logger::Debug() << "Start parsing admin log list request parameters";

            AdminLogListRequest request;

            /// 解析可选参数 page
            auto page_result = QueryPositiveInt(req, "page", 1);
            if (!page_result) return std::unexpected(page_result.error());
            if (page_result->has_value()) {
                request.page = **page_result;
            }

            /// 解析可选参数 page_size
            auto page_size_result = QueryPositiveInt(req, "page_size", 1, 100);
            if (!page_size_result) return std::unexpected(page_size_result.error());
            if (page_size_result->has_value()) {
                request.page_size = **page_size_result;
            }

            /// 解析可选参数 action
            auto action_str = req->getParameter("action");
            if (!action_str.empty()) {
                request.action = action_str;
            }

            /// 解析可选参数 start_date
            auto start_date_str = req->getParameter("start_date");
            if (!start_date_str.empty()) {
                request.start_date = start_date_str;
            }

            /// 解析可选参数 end_date
            auto end_date_str = req->getParameter("end_date");
            if (!end_date_str.empty()) {
                request.end_date = end_date_str;
            }

            Logger::Debug() << "Parsed admin log list request: page=" << request.page
                      << ", page_size=" << request.page_size
                      << ", action=" << (request.action.has_value() ? *request.action : "null")
                      << ", start_date=" << (request.start_date.has_value() ? *request.start_date : "null")
                      << ", end_date=" << (request.end_date.has_value() ? *request.end_date : "null");

            return request;
        }
    };

    /// ==================== Response DTOs ====================

    /**
     * @brief 用户详情响应 DTO
     *
     * @details
     * 包含用户的完整信息，用于管理员查看用户详情。
     */
    struct UserDetailResponse : DtoBase<UserDetailResponse> {
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
            SetField(json, "id", id);
            SetField(json, "username", username);
            SetField(json, "email", email);
            SetField(json, "nickname", nickname);
            SetField(json, "avatar", avatar);
            SetField(json, "role", role);
            SetField(json, "status", status);
            SetField(json, "storage_quota", storage_quota);
            SetField(json, "storage_used", storage_used);
            SetField(json, "storage_reserved", storage_reserved);
            SetField(json, "created_at", created_at);
            SetField(json, "last_login_at", last_login_at);
            return json;
        }
    };

    /**
     * @brief 用户列表响应 DTO
     *
     * @details
     * 包含用户列表和分页信息。
     */
    struct UserListResponse : DtoBase<UserListResponse> {
        std::vector<UserDetailResponse> items;
        PaginationInfo pagination;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetArray(json, "items", items);
            SetField(json, "pagination", pagination);
            return json;
        }
    };

    /**
     * @brief 存储统计响应 DTO
     *
     * @details
     * 包含系统整体存储统计信息。
     */
    struct StorageStatsResponse : DtoBase<StorageStatsResponse> {
        int total_users{ 0 };
        int total_files{ 0 };
        uint64_t total_storage_used{ 0 };
        uint64_t total_storage_quota{ 0 };
        int active_shares{ 0 };

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "total_users", total_users);
            SetField(json, "total_files", total_files);
            SetField(json, "total_storage_used", total_storage_used);
            SetField(json, "total_storage_quota", total_storage_quota);
            SetField(json, "active_shares", active_shares);
            return json;
        }
    };

    /**
     * @brief 系统状态响应 DTO
     *
     * @details
     * 包含系统运行状态和资源使用情况。
     */
    struct SystemStatusResponse : DtoBase<SystemStatusResponse> {
        bool db_connected{ false };
        bool redis_connected{ false };
        uint64_t disk_total{ 0 };
        uint64_t disk_used{ 0 };
        uint64_t disk_free{ 0 };
        uint64_t uptime_seconds{ 0 };

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "db_connected", db_connected);
            SetField(json, "redis_connected", redis_connected);
            SetField(json, "disk_total", disk_total);
            SetField(json, "disk_used", disk_used);
            SetField(json, "disk_free", disk_free);
            SetField(json, "uptime_seconds", uptime_seconds);
            return json;
        }
    };

    /**
     * @brief 分享详情响应 DTO
     *
     * @details
     * 包含分享的详细信息，用于管理员查看分享详情。
     */
    struct ShareDetailResponse : DtoBase<ShareDetailResponse> {
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
            SetField(json, "id", id);
            SetField(json, "user_id", user_id);
            SetField(json, "username", username);
            SetField(json, "file_id", file_id);
            SetField(json, "file_name", file_name);
            SetField(json, "share_code", share_code);
            SetField(json, "status", status);
            SetField(json, "access_count", access_count);
            SetField(json, "password_set", password_set);
            SetField(json, "created_at", created_at);
            SetField(json, "expires_at", expires_at);
            return json;
        }
    };

    /**
     * @brief 分享列表响应 DTO
     *
     * @details
     * 包含分享列表和分页信息。
     */
    struct ShareListResponse : DtoBase<ShareListResponse> {
        std::vector<ShareDetailResponse> items;
        PaginationInfo pagination;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetArray(json, "items", items);
            SetField(json, "pagination", pagination);
            return json;
        }
    };

    /**
     * @brief 操作日志详情响应 DTO
     *
     * @details
     * 包含单条操作日志的完整信息。
     */
    struct AdminLogDetailResponse : DtoBase<AdminLogDetailResponse> {
        uint64_t id;
        uint64_t user_id;
        std::string action;
        std::string target_type;
        std::optional<uint64_t> target_id;
        std::optional<std::string> details;
        std::string ip_address;
        std::string created_at;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "id", id);
            SetField(json, "user_id", user_id);
            SetField(json, "action", action);
            SetField(json, "target_type", target_type);
            SetOptionalOrNull(json, "target_id", target_id);
            SetOptionalOrNull(json, "details", details);
            SetField(json, "ip_address", ip_address);
            SetField(json, "created_at", created_at);
            return json;
        }
    };

    /**
     * @brief 操作日志列表响应 DTO
     *
     * @details
     * 包含操作日志列表和分页信息。
     */
    struct AdminLogListResponse : DtoBase<AdminLogListResponse> {
        std::vector<AdminLogDetailResponse> items;
        PaginationInfo pagination;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetArray(json, "items", items);
            SetField(json, "pagination", pagination);
            return json;
        }
    };

} ///< namespace disk::admin
