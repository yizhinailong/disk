/**
 * @file ShareDto.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 分享模块数据传输对象
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 本文件包含分享模块的所有数据传输对象（DTO）：
 * - CreateShareRequest: 创建分享请求
 * - CreateShareResponse: 创建分享响应
 * - ShareListRequest: 获取分享列表请求
 * - ShareItem: 分享项（列表响应组件）
 * - ShareListResponse: 获取分享列表响应
 * - ShareDetailRequest: 获取分享详情请求
 * - ShareFile: 分享文件项
 * - ShareDetailResponse: 获取分享详情响应
 * - UpdateShareRequest: 更新分享设置请求
 * - UpdateShareResponse: 更新分享设置响应
 * - CancelShareRequest: 取消分享请求
 * - CancelShareResult: 取消分享单项结果
 * - CancelShareSummary: 取消分享汇总
 * - CancelShareResponse: 取消分享响应
 * - AccessShareRequest: 验证分享访问请求
 * - AccessShareResponse: 验证分享访问响应
 * - BrowseShareRequest: 浏览分享内容请求
 * - BrowseItem: 浏览内容项
 * - BrowseBreadcrumb: 浏览面包屑
 * - BrowseShareResponse: 浏览分享内容响应
 * - DownloadShareRequest: 下载分享文件请求
 *
 * DTO 用于在不同层（Controller、Service）之间传输数据，
 * 包含请求验证和响应序列化逻辑。
 */

#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include <drogon/HttpRequest.h>
#include <json/json.h>

#include "utils/ErrorCode.hpp"

namespace disk::share {

    // ==================== 分页结构 ====================

    /**
     * @brief 分页信息
     */
    struct Pagination {
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

    // ==================== 分享状态枚举 ====================

    /**
     * @brief 分享状态
     */
    enum class ShareStatus {
        Cancelled = 0, ///< 已取消
        Active = 1,    ///< 有效
        Expired = 2    ///< 已过期
    };

    /**
     * @brief 分享状态转换为字符串
     */
    [[nodiscard]]
    inline auto ShareStatusToString(ShareStatus status) -> std::string {
        switch (status) {
            case ShareStatus::Active   : return "active";
            case ShareStatus::Expired  : return "expired";
            case ShareStatus::Cancelled: return "cancelled";
            default                    : return "unknown";
        }
    }

    // ==================== 权限枚举 ====================

    /**
     * @brief 分享权限
     */
    enum class SharePermission {
        View,    ///< 仅查看
        Download ///< 可下载
    };

    /**
     * @brief 分享权限转换为字符串
     */
    [[nodiscard]]
    inline auto SharePermissionToString(SharePermission permission) -> std::string {
        return permission == SharePermission::Download ? "download" : "view";
    }

    /**
     * @brief 字符串转换为分享权限
     * @param str 字符串（"view" 或 "download"）
     * @return 分享权限，无效时返回 nullopt
     */
    [[nodiscard]]
    inline auto StringToSharePermission(const std::string& str) -> std::optional<SharePermission> {
        if (str == "download") {
            return SharePermission::Download;
        }
        if (str == "view") {
            return SharePermission::View;
        }
        return std::nullopt;
    }

    [[nodiscard]]
    inline auto ParseOptionalPositiveIdArray(
        const Json::Value& json,
        const char* field,
        std::vector<uint64_t>& ids
    ) -> Result<void> {
        if (!json.isMember(field)) {
            return {};
        }
        if (!json[field].isArray()) {
            LOG_WARN << "Parameter '" << field << "' type error: expected array";
            return std::unexpected(ErrorInfo(
                ErrorCode::InvalidParameter,
                std::string("Parameter '") + field + "' type error: expected array"
            ));
        }

        for (const auto& item : json[field]) {
            if (!item.isIntegral()) {
                LOG_WARN << "Element in parameter '" << field << "' type error: expected integer";
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidParameter,
                    std::string("Element in parameter '") + field + "' type error: expected integer"
                ));
            }
            auto id = item.asUInt64();
            if (id == 0) {
                LOG_WARN << "Element in parameter '" << field << "' must be a positive integer";
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidParameter,
                    std::string("Element in parameter '") + field + "' must be a positive integer"
                ));
            }
            ids.push_back(id);
        }
        return {};
    }

    // ==================== 共享组件 ====================

    /**
     * @brief 分享文件项
     *
     * @details
     * 用于表示分享中的单个文件/文件夹信息。
     */
    struct ShareFile {
        uint64_t id;
        std::string name;
        std::string type; // "file" 或 "folder"
        uint64_t size;
        uint32_t item_count{ 0 };

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["id"] = static_cast<Json::UInt64>(id);
            json["name"] = name;
            json["type"] = type;
            json["size"] = static_cast<Json::UInt64>(size);
            if (type == "folder") {
                json["item_count"] = item_count;
            }
            return json;
        }
    };

    // ==================== Create Share ====================

    /**
     * @brief 创建分享请求 DTO
     *
     * @details
     * 验证规则：
     * - file_ids/folder_ids: 至少一个非空数组，每个元素为正整数
     * - expire_days: 默认 7 天，0 表示永久，必须 >= 0
     * - password: 可选，4-8 字符
     * - permission: 默认 download，可选值 view/download
     */
    struct CreateShareRequest {
        std::vector<uint64_t> file_ids;
        std::vector<uint64_t> folder_ids;
        int expire_days{ 7 };
        std::optional<std::string> password;
        SharePermission permission{ SharePermission::Download };

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<CreateShareRequest> {
            LOG_DEBUG << "Start parsing create share request parameters";

            auto json_ptr = req->getJsonObject();
            if (!json_ptr) {
                LOG_WARN << "Request body is not valid JSON";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Request body is not valid JSON")
                );
            }

            const auto& json = *json_ptr;
            CreateShareRequest request;

            auto file_ids_result = ParseOptionalPositiveIdArray(json, "file_ids", request.file_ids);
            if (!file_ids_result) {
                return std::unexpected(file_ids_result.error());
            }
            auto folder_ids_result = ParseOptionalPositiveIdArray(json, "folder_ids", request.folder_ids);
            if (!folder_ids_result) {
                return std::unexpected(folder_ids_result.error());
            }

            if (request.file_ids.empty() && request.folder_ids.empty()) {
                LOG_WARN << "Create share request must contain file_ids or folder_ids";
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidParameter,
                    "Create share request must contain file_ids or folder_ids"
                ));
            }

            // 解析可选参数 expire_days
            if (json.isMember("expire_days")) {
                if (!json["expire_days"].isIntegral()) {
                    LOG_WARN << "Parameter 'expire_days' type error: expected integer";
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'expire_days' type error: expected integer"
                    ));
                }
                request.expire_days = json["expire_days"].asInt();
                if (request.expire_days < 0) {
                    LOG_WARN << "Parameter 'expire_days' cannot be negative: "
                             << request.expire_days;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'expire_days' cannot be negative"
                    ));
                }
            }

            // 解析可选参数 password
            if (json.isMember("password")) {
                if (!json["password"].isString()) {
                    LOG_WARN << "Parameter 'password' type error: expected string";
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'password' type error: expected string"
                    ));
                }
                std::string pwd = json["password"].asString();
                if (!pwd.empty()) {
                    if (pwd.length() < 4 || pwd.length() > 8) {
                        LOG_WARN << "Access password length must be between 4-8 characters: "
                                 << pwd.length();
                        return std::unexpected(ErrorInfo(
                            ErrorCode::ValidationFailed,
                            "Access password length must be between 4-8 characters"
                        ));
                    }
                    request.password = pwd;
                }
            }

            // 解析可选参数 permission
            if (json.isMember("permission")) {
                if (!json["permission"].isString()) {
                    LOG_WARN << "Parameter 'permission' type error: expected string";
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'permission' type error: expected string"
                    ));
                }
                std::string perm_str = json["permission"].asString();
                auto perm_opt = StringToSharePermission(perm_str);
                if (!perm_opt) {
                    LOG_WARN << "Parameter 'permission' invalid value: " << perm_str;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'permission' invalid value, must be view or download"
                    ));
                }
                request.permission = *perm_opt;
            }

            LOG_DEBUG << "Parsed create share request: file_ids.size()=" << request.file_ids.size()
                      << ", folder_ids.size()=" << request.folder_ids.size()
                      << ", expire_days=" << request.expire_days
                      << ", has_password=" << request.password.has_value()
                      << ", permission=" << SharePermissionToString(request.permission);

            return request;
        }
    };

    /**
     * @brief 创建分享响应 DTO
     *
     * @details
     * 包含分享的基本信息。
     */
    struct CreateShareResponse {
        std::string share_id;
        std::string share_link;
        std::optional<std::string> password;
        std::string permission;
        std::string expires_at;
        std::string created_at;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["share_id"] = share_id;
            json["share_link"] = share_link;
            if (password.has_value()) {
                json["password"] = *password;
            }
            json["permission"] = permission;
            json["expires_at"] = expires_at;
            json["created_at"] = created_at;
            return json;
        }
    };

    // ==================== Share List ====================

    /**
     * @brief 获取分享列表请求 DTO
     *
     * @details
     * 验证规则：
     * - status: 可选，默认 "all"，可选值 all/active/expired/cancelled
     * - page: 默认 1，必须 > 0
     * - page_size: 默认 20，必须 > 0 且 <= 100
     *
     * 从 URL 查询参数解析。
     */
    struct ShareListRequest {
        std::string status{ "all" };
        int page{ 1 };
        int page_size{ 20 };

        /// 从 HTTP 请求查询参数解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<ShareListRequest> {
            LOG_DEBUG << "Start parsing share list request parameters";

            ShareListRequest request;

            // 有效状态值
            static const std::set<std::string> valid_statuses = { "all",
                                                                  "active",
                                                                  "expired",
                                                                  "cancelled" };

            // 解析可选参数 status
            auto status_str = req->getParameter("status");
            if (!status_str.empty()) {
                if (valid_statuses.find(status_str) == valid_statuses.end()) {
                    LOG_WARN << "Parameter 'status' invalid value: " << status_str;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'status' invalid value, must be all/active/expired/cancelled"
                    ));
                }
                request.status = status_str;
            }

            // 解析可选参数 page
            auto page_str = req->getParameter("page");
            if (!page_str.empty()) {
                try {
                    size_t pos = 0;
                    auto value = std::stoi(page_str, &pos);
                    if (pos != page_str.length() || value <= 0) {
                        LOG_WARN << "Parameter 'page' invalid format or value: " << page_str;
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
                    if (pos != page_size_str.length() || value <= 0 || value > 100) {
                        LOG_WARN << "Parameter 'page_size' invalid format or value: "
                                 << page_size_str;
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

            LOG_DEBUG << "Parsed share list request: status=" << request.status
                      << ", page=" << request.page << ", page_size=" << request.page_size;

            return request;
        }
    };

    /**
     * @brief 分享项数据
     *
     * @details
     * 用于表示分享列表中的单个分享项。
     */
    struct ShareItem {
        std::string share_id;
        std::string file_name;
        int file_count{ 0 };
        std::string share_link;
        bool has_password{ false };
        std::string permission;
        int view_count{ 0 };
        int download_count{ 0 };
        std::string created_at;
        std::string expires_at;
        std::string status;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["share_id"] = share_id;
            json["file_name"] = file_name;
            json["file_count"] = file_count;
            json["share_link"] = share_link;
            json["has_password"] = has_password;
            json["permission"] = permission;
            json["view_count"] = view_count;
            json["download_count"] = download_count;
            json["created_at"] = created_at;
            json["expires_at"] = expires_at;
            json["status"] = status;
            return json;
        }
    };

    /**
     * @brief 分享列表响应 DTO
     *
     * @details
     * 包含分享项列表和分页信息。
     */
    struct ShareListResponse {
        std::vector<ShareItem> items;
        Pagination pagination;

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

    // ==================== Share Detail ====================

    /**
     * @brief 获取分享详情请求 DTO（路径参数）
     *
     * @details
     * 验证规则：
     * - share_id: 非空字符串
     *
     * 从 URL 路径参数解析。
     */
    struct ShareDetailRequest {
        std::string share_id;

        /// 从路径参数解析并验证，返回 Result
        [[nodiscard]]
        static auto FromPath(const std::string& share_id_str) -> Result<ShareDetailRequest> {
            LOG_DEBUG << "Start parsing share detail request parameters";

            if (share_id_str.empty()) {
                LOG_WARN << "Missing required parameter: share_id";
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "Missing required parameter: share_id")
                );
            }

            ShareDetailRequest request;
            request.share_id = share_id_str;

            LOG_DEBUG << "Parsed share detail request: share_id=" << request.share_id;

            return request;
        }
    };

    /**
     * @brief 分享详情响应 DTO
     *
     * @details
     * 包含分享的详细信息和文件列表。
     */
    struct ShareDetailResponse {
        std::string share_id;
        std::vector<ShareFile> files;
        std::string share_link;
        bool has_password{ false };
        std::string permission;
        int view_count{ 0 };
        int download_count{ 0 };
        std::string created_at;
        std::string expires_at;
        std::string status;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["share_id"] = share_id;

            Json::Value files_array(Json::arrayValue);
            for (const auto& file : files) {
                files_array.append(file.ToJson());
            }
            json["files"] = files_array;

            json["share_link"] = share_link;
            json["has_password"] = has_password;
            json["permission"] = permission;
            json["view_count"] = view_count;
            json["download_count"] = download_count;
            json["created_at"] = created_at;
            json["expires_at"] = expires_at;
            json["status"] = status;
            return json;
        }
    };

    // ==================== Update Share ====================

    /**
     * @brief 更新分享设置请求 DTO
     *
     * @details
     * 验证规则：
     * - share_id: 非空字符串（路径参数）
     * - expire_days: 可选，>= 0
     * - password: 可选，4-8 字符，空字符串表示移除密码
     * - permission: 可选，view/download
     */
    struct UpdateShareRequest {
        std::string share_id;
        std::optional<int> expire_days;
        std::optional<std::string> password; // 空字符串表示移除密码
        std::optional<SharePermission> permission;

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req, const std::string& share_id_str)
            -> Result<UpdateShareRequest> {
            LOG_DEBUG << "Start parsing update share settings request parameters";

            // 验证路径参数 share_id
            if (share_id_str.empty()) {
                LOG_WARN << "Missing required parameter: share_id";
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "Missing required parameter: share_id")
                );
            }

            auto json_ptr = req->getJsonObject();
            if (!json_ptr) {
                LOG_WARN << "Request body is not valid JSON";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Request body is not valid JSON")
                );
            }

            const auto& json = *json_ptr;

            UpdateShareRequest request;
            request.share_id = share_id_str;

            // 解析可选参数 expire_days
            if (json.isMember("expire_days")) {
                if (!json["expire_days"].isIntegral()) {
                    LOG_WARN << "Parameter 'expire_days' type error: expected integer";
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'expire_days' type error: expected integer"
                    ));
                }
                int expire_days = json["expire_days"].asInt();
                if (expire_days < 0) {
                    LOG_WARN << "Parameter 'expire_days' cannot be negative: " << expire_days;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'expire_days' cannot be negative"
                    ));
                }
                request.expire_days = expire_days;
            }

            // 解析可选参数 password
            if (json.isMember("password")) {
                if (!json["password"].isString()) {
                    LOG_WARN << "Parameter 'password' type error: expected string";
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'password' type error: expected string"
                    ));
                }
                std::string pwd = json["password"].asString();
                // 空字符串表示移除密码，非空时验证长度
                if (!pwd.empty() && (pwd.length() < 4 || pwd.length() > 8)) {
                    LOG_WARN << "Access password length must be between 4-8 characters: "
                             << pwd.length();
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Access password length must be between 4-8 characters"
                    ));
                }
                request.password = pwd;
            }

            // 解析可选参数 permission
            if (json.isMember("permission")) {
                if (!json["permission"].isString()) {
                    LOG_WARN << "Parameter 'permission' type error: expected string";
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'permission' type error: expected string"
                    ));
                }
                std::string perm_str = json["permission"].asString();
                auto perm_opt = StringToSharePermission(perm_str);
                if (!perm_opt) {
                    LOG_WARN << "Parameter 'permission' invalid value: " << perm_str;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'permission' invalid value, must be view or download"
                    ));
                }
                request.permission = *perm_opt;
            }

            LOG_DEBUG << "Parsed update share settings request: share_id=" << request.share_id
                      << ", expire_days="
                      << (request.expire_days.has_value() ? std::to_string(*request.expire_days) :
                                                            "null")
                      << ", password=" << (request.password.has_value() ? "set" : "null")
                      << ", permission="
                      << (request.permission.has_value() ?
                              SharePermissionToString(*request.permission) :
                              "null");

            return request;
        }
    };

    /**
     * @brief 更新分享设置响应 DTO
     *
     * @details
     * 包含更新后的分享信息。
     */
    struct UpdateShareResponse {
        std::string share_id;
        std::string expires_at;
        bool has_password{ false };
        std::string permission;
        std::string updated_at;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["share_id"] = share_id;
            json["expires_at"] = expires_at;
            json["has_password"] = has_password;
            json["permission"] = permission;
            json["updated_at"] = updated_at;
            return json;
        }
    };

    // ==================== Cancel Share ====================

    /**
     * @brief 取消分享请求 DTO
     *
     * @details
     * 验证规则：
     * - share_ids: 非空数组，每个元素为非空字符串
     */
    struct CancelShareRequest {
        std::vector<std::string> share_ids;

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<CancelShareRequest> {
            LOG_DEBUG << "Start parsing cancel share request parameters";

            auto json_ptr = req->getJsonObject();
            if (!json_ptr) {
                LOG_WARN << "Request body is not valid JSON";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Request body is not valid JSON")
                );
            }

            const auto& json = *json_ptr;

            // 检查必填字段 share_ids
            if (!json.isMember("share_ids")) {
                LOG_WARN << "Missing required parameter: share_ids";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Missing required parameter: share_ids")
                );
            }

            if (!json["share_ids"].isArray()) {
                LOG_WARN << "Parameter 'share_ids' type error: expected array";
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidParameter,
                    "Parameter 'share_ids' type error: expected array"
                ));
            }

            CancelShareRequest request;

            // 解析 share_ids
            const auto& share_ids_array = json["share_ids"];
            if (share_ids_array.empty()) {
                LOG_WARN << "Parameter 'share_ids' cannot be empty array";
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidParameter,
                    "Parameter 'share_ids' cannot be empty array"
                ));
            }

            for (const auto& item : share_ids_array) {
                if (!item.isString()) {
                    LOG_WARN << "Element type error in parameter 'share_ids': expected string";
                    return std::unexpected(ErrorInfo(
                        ErrorCode::InvalidParameter,
                        "Element type error in parameter 'share_ids': expected string"
                    ));
                }
                std::string share_id = item.asString();
                if (share_id.empty()) {
                    LOG_WARN << "Element in parameter 'share_ids' cannot be empty string";
                    return std::unexpected(ErrorInfo(
                        ErrorCode::InvalidParameter,
                        "Element in parameter 'share_ids' cannot be empty string"
                    ));
                }
                request.share_ids.push_back(share_id);
            }

            LOG_DEBUG << "Parsed cancel share request: share_ids.size()="
                      << request.share_ids.size();

            return request;
        }
    };

    /**
     * @brief 取消分享单项错误信息
     */
    struct CancelShareError {
        int code{ 0 };
        std::string message;
        std::string reason;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["code"] = code;
            json["message"] = message;
            json["reason"] = reason;
            return json;
        }
    };

    /**
     * @brief 取消分享单项结果
     */
    struct CancelShareResult {
        std::string share_id;
        std::string status; // "success" 或 "failed"
        std::optional<CancelShareError> error;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["share_id"] = share_id;
            json["status"] = status;
            if (error.has_value()) {
                json["error"] = error->ToJson();
            }
            return json;
        }
    };

    /**
     * @brief 取消分享汇总
     */
    struct CancelShareSummary {
        int total{ 0 };
        int succeeded{ 0 };
        int failed{ 0 };

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["total"] = total;
            json["succeeded"] = succeeded;
            json["failed"] = failed;
            return json;
        }
    };

    /**
     * @brief 取消分享响应 DTO
     *
     * @details
     * 包含取消操作的汇总和每项结果。
     */
    struct CancelShareResponse {
        CancelShareSummary summary;
        std::vector<CancelShareResult> results;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["summary"] = summary.ToJson();

            Json::Value results_array(Json::arrayValue);
            for (const auto& result : results) {
                results_array.append(result.ToJson());
            }
            json["results"] = results_array;

            return json;
        }
    };

    // ==================== Access Share ====================

    /**
     * @brief 验证分享访问请求 DTO
     *
     * @details
     * 验证规则：
     * - share_id: 非空字符串（路径参数）
     * - password: 可选，访问密码
     */
    struct AccessShareRequest {
        std::string share_id;
        std::optional<std::string> password;

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req, const std::string& share_id_str)
            -> Result<AccessShareRequest> {
            LOG_DEBUG << "Start parsing access share verification request parameters";

            // 验证路径参数 share_id
            if (share_id_str.empty()) {
                LOG_WARN << "Missing required parameter: share_id";
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "Missing required parameter: share_id")
                );
            }

            AccessShareRequest request;
            request.share_id = share_id_str;

            // 解析可选的请求体
            auto json_ptr = req->getJsonObject();
            if (json_ptr) {
                const auto& json = *json_ptr;

                // 解析可选参数 password
                if (json.isMember("password")) {
                    if (!json["password"].isString()) {
                        LOG_WARN << "Parameter 'password' type error: expected string";
                        return std::unexpected(ErrorInfo(
                            ErrorCode::ValidationFailed,
                            "Parameter 'password' type error: expected string"
                        ));
                    }
                    std::string pwd = json["password"].asString();
                    if (!pwd.empty()) {
                        request.password = pwd;
                    }
                }
            }

            LOG_DEBUG << "Parsed access share verification request: share_id=" << request.share_id
                      << ", has_password=" << request.password.has_value();

            return request;
        }
    };

    /**
     * @brief 验证分享访问响应 DTO
     *
     * @details
     * 包含分享令牌和文件列表。
     */
    struct AccessShareResponse {
        std::string share_token;
        int expires_in{ 0 };
        std::string permission;
        std::vector<ShareFile> files;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["share_token"] = share_token;
            json["expires_in"] = expires_in;
            json["permission"] = permission;

            Json::Value files_array(Json::arrayValue);
            for (const auto& file : files) {
                files_array.append(file.ToJson());
            }
            json["files"] = files_array;

            return json;
        }
    };

    // ==================== Browse Share ====================

    /**
     * @brief 浏览分享内容请求 DTO
     *
     * @details
     * 验证规则：
     * - share_id: 非空字符串（路径参数）
     * - folder_id: 可选，文件夹 ID（查询参数）
     *
     * 从 URL 路径和查询参数解析。
     */
    struct BrowseShareRequest {
        std::string share_id;
        std::optional<uint64_t> folder_id;

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req, const std::string& share_id_str)
            -> Result<BrowseShareRequest> {
            LOG_DEBUG << "Start parsing browse share content request parameters";

            // 验证路径参数 share_id
            if (share_id_str.empty()) {
                LOG_WARN << "Missing required parameter: share_id";
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "Missing required parameter: share_id")
                );
            }

            BrowseShareRequest request;
            request.share_id = share_id_str;

            // 解析可选参数 folder_id
            auto folder_id_str = req->getParameter("folder_id");
            if (!folder_id_str.empty()) {
                try {
                    size_t pos = 0;
                    auto value = std::stoull(folder_id_str, &pos);
                    if (pos != folder_id_str.length()) {
                        LOG_WARN << "Parameter 'folder_id' invalid format: " << folder_id_str;
                        return std::unexpected(ErrorInfo(
                            ErrorCode::ValidationFailed,
                            "Parameter 'folder_id' invalid format"
                        ));
                    }
                    request.folder_id = value;
                } catch (const std::exception& e) {
                    LOG_WARN << "Parameter 'folder_id' invalid format: " << folder_id_str;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'folder_id' invalid format"
                    ));
                }
            }

            LOG_DEBUG << "Parsed browse share content request: share_id=" << request.share_id
                      << ", folder_id="
                      << (request.folder_id.has_value() ? std::to_string(*request.folder_id) :
                                                          "null");

            return request;
        }
    };

    /**
     * @brief 浏览内容项
     */
    struct BrowseItem {
        uint64_t id;
        std::string name;
        std::string type; // "file" 或 "folder"
        uint64_t size;
        uint32_t item_count{ 0 };

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["id"] = static_cast<Json::UInt64>(id);
            json["name"] = name;
            json["type"] = type;
            json["size"] = static_cast<Json::UInt64>(size);
            if (type == "folder") {
                json["item_count"] = item_count;
            }
            return json;
        }
    };

    /**
     * @brief 浏览面包屑项
     */
    struct BrowseBreadcrumb {
        uint64_t id;
        std::string name;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["id"] = static_cast<Json::UInt64>(id);
            json["name"] = name;
            return json;
        }
    };

    /**
     * @brief 浏览分享内容响应 DTO
     *
     * @details
     * 包含内容项列表和面包屑导航。
     */
    struct BrowseShareResponse {
        std::vector<BrowseItem> items;
        std::vector<BrowseBreadcrumb> breadcrumb;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;

            Json::Value items_array(Json::arrayValue);
            for (const auto& item : items) {
                items_array.append(item.ToJson());
            }
            json["items"] = items_array;

            Json::Value breadcrumb_array(Json::arrayValue);
            for (const auto& item : breadcrumb) {
                breadcrumb_array.append(item.ToJson());
            }
            json["breadcrumb"] = breadcrumb_array;

            return json;
        }
    };

    // ==================== Download Share ====================

    /**
     * @brief 下载分享文件请求 DTO
     *
     * @details
     * 验证规则：
     * - share_id: 非空字符串（路径参数）
     * - file_id: 正整数（路径参数）
     *
     * 从 URL 路径参数解析。
     */
    struct DownloadShareRequest {
        std::string share_id;
        uint64_t file_id{ 0 };

        /// 从路径参数解析并验证，返回 Result
        [[nodiscard]]
        static auto FromPath(const std::string& share_id_str, const std::string& file_id_str)
            -> Result<DownloadShareRequest> {
            LOG_DEBUG << "Start parsing download share file request parameters";

            // 验证路径参数 share_id
            if (share_id_str.empty()) {
                LOG_WARN << "Missing required parameter: share_id";
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "Missing required parameter: share_id")
                );
            }

            // 验证路径参数 file_id
            if (file_id_str.empty()) {
                LOG_WARN << "Missing required parameter: file_id";
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "Missing required parameter: file_id")
                );
            }

            // 检查是否为负数（stoull 会将负数回绕）
            if (file_id_str[0] == '-') {
                LOG_WARN << "Parameter 'file_id' invalid format or value: " << file_id_str;
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidParameter,
                    "Parameter 'file_id' must be a positive integer"
                ));
            }

            uint64_t file_id = 0;
            try {
                size_t pos = 0;
                file_id = std::stoull(file_id_str, &pos);
                if (pos != file_id_str.length() || file_id == 0) {
                    LOG_WARN << "Parameter 'file_id' invalid format or value: " << file_id_str;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::InvalidParameter,
                        "Parameter 'file_id' must be a positive integer"
                    ));
                }
            } catch (const std::exception& e) {
                LOG_WARN << "Parameter 'file_id' invalid format: " << file_id_str;
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "Parameter 'file_id' invalid format")
                );
            }

            DownloadShareRequest request;
            request.share_id = share_id_str;
            request.file_id = file_id;

            LOG_DEBUG << "Parsed download share file request: share_id=" << request.share_id
                      << ", file_id=" << request.file_id;

            return request;
        }
    };

    // ==================== Save Share Items ====================

    struct SaveShareItemsRequest {
        std::string share_id;
        std::vector<uint64_t> file_ids;
        std::vector<uint64_t> folder_ids;
        uint64_t target_folder_id{ 0 };

        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req, const std::string& share_id_str)
            -> Result<SaveShareItemsRequest> {
            LOG_DEBUG << "Start parsing save share items request parameters";

            if (share_id_str.empty()) {
                LOG_WARN << "Missing required parameter: share_id";
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "Missing required parameter: share_id")
                );
            }

            auto json_ptr = req->getJsonObject();
            if (!json_ptr) {
                LOG_WARN << "Request body is not valid JSON";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Request body is not valid JSON")
                );
            }

            const auto& json = *json_ptr;
            SaveShareItemsRequest request;
            request.share_id = share_id_str;

            auto file_ids_result = ParseOptionalPositiveIdArray(json, "file_ids", request.file_ids);
            if (!file_ids_result) {
                return std::unexpected(file_ids_result.error());
            }
            auto folder_ids_result = ParseOptionalPositiveIdArray(json, "folder_ids", request.folder_ids);
            if (!folder_ids_result) {
                return std::unexpected(folder_ids_result.error());
            }

            if (request.file_ids.empty() && request.folder_ids.empty()) {
                LOG_WARN << "Save share request must contain file_ids or folder_ids";
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidParameter,
                    "Save share request must contain file_ids or folder_ids"
                ));
            }

            if (json.isMember("target_folder_id")) {
                if (!json["target_folder_id"].isIntegral()) {
                    LOG_WARN << "Parameter 'target_folder_id' type error: expected integer";
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'target_folder_id' type error: expected integer"
                    ));
                }
                request.target_folder_id = json["target_folder_id"].asUInt64();
            }

            LOG_DEBUG << "Parsed save share request: share_id=" << request.share_id
                      << ", file_ids.size()=" << request.file_ids.size()
                      << ", folder_ids.size()=" << request.folder_ids.size()
                      << ", target_folder_id=" << request.target_folder_id;

            return request;
        }
    };

    struct SaveShareItemsResponse {
        int saved_count{ 0 };
        int saved_file_count{ 0 };
        int saved_folder_count{ 0 };

        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["saved_count"] = saved_count;
            json["saved_file_count"] = saved_file_count;
            json["saved_folder_count"] = saved_folder_count;
            return json;
        }
    };

    // ==================== Download Info ====================

    /**
     * @brief 下载文件信息
     *
     * @details
     * 包含文件下载所需的元数据，用于控制器构建响应。
     */
    struct DownloadInfo {
        uint64_t file_id{ 0 };
        std::string filename;
        std::string storage_path;
        uint64_t file_size{ 0 };
        std::string mime_type;
        std::string hash_md5;
    };

    } // namespace disk::share
