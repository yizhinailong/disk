/**
 * @file FolderDto.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件夹模块数据传输对象
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 本文件包含文件夹模块的所有数据传输对象（DTO）：
 * - CreateFolderRequest: 创建文件夹请求
 * - CreateFolderResponse: 创建文件夹响应
 * - FolderTreeRequest: 获取文件夹树请求
 * - FolderNodeData: 文件夹节点数据（内部结构）
 * - FolderTreeNode: 文件夹树节点响应
 * - BreadcrumbItem: 面包屑导航项
 * - BreadcrumbResponse: 面包屑导航响应
 *
 * DTO 用于在不同层（Controller、Service）之间传输数据，
 * 包含请求验证和响应序列化逻辑。
 */

#pragma once

#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <json/json.h>

#include "utils/DtoBase.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/LogHelper.hpp"
#include "utils/NameValidation.hpp"

namespace disk::folder {

    namespace folder_dto_detail {
        [[nodiscard]] inline auto TrimAsciiSpaces(std::string value) -> std::string {
            const auto start = value.find_first_not_of(' ');
            if (start == std::string::npos) {
                return {};
            }
            const auto end = value.find_last_not_of(' ');
            return value.substr(start, end - start + 1);
        }
    } // namespace folder_dto_detail

    /// ==================== Request DTOs ====================

    /**
     * @brief 创建文件夹请求 DTO
     *
     * @details
     * 验证规则：
     * - name: 1-255字符（去除首尾空格后）
     * - name: 禁止字符 / \ : * ? " < > | 及控制字符 (0x00-0x1F)
     * - name: 禁止保留名称 "." 和 ".."
     * - name: 禁止以 "." 开头（隐藏文件夹）
     * - name: 必须是合法 UTF-8，禁止控制字符
     * - parent_id: 默认 0（根目录）
     */
    struct CreateFolderRequest : DtoBase<CreateFolderRequest> {
        std::string name;
        uint64_t parent_id{ 0 };

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(
            const drogon::HttpRequestPtr& req,
            disk::utils::LogContext log_context = {}
        ) -> Result<CreateFolderRequest> {
            Logger::Debug(log_context) << "Start parsing create folder request parameters";

            auto json_result = RequireJsonBody(req);
            if (!json_result) {
                return std::unexpected(json_result.error());
            }
            const auto& json = *json_result.value();

            auto name_result = RequireString(json, "name");
            if (!name_result) {
                return std::unexpected(name_result.error());
            }

            CreateFolderRequest request;
            request.name = std::move(*name_result);

            auto parent_id_result = OptionalUInt64(json, "parent_id");
            if (!parent_id_result) {
                return std::unexpected(parent_id_result.error());
            }
            if (parent_id_result->has_value()) {
                request.parent_id = **parent_id_result;
            }

            /// 规则 6：去除首尾空格
            request.name = folder_dto_detail::TrimAsciiSpaces(std::move(request.name));

            Logger::Debug(log_context) << "Parsed create folder request: name=\"" << request.name
                                       << "\", parent_id=" << request.parent_id;

            /// 规则 1：长度验证 (1-255)
            if (!request.ValidateLength()) {
                Logger::Warn(log_context) << "Invalid folder name length: " << request.name.length();
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "Folder name length must be between 1-255 characters"
                ));
            }

            /// 规则 2：禁止字符验证
            if (!request.ValidateForbiddenChars()) {
                Logger::Warn(log_context) << "Folder name contains forbidden characters: " << request.name;
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidFilename,
                    "Folder name contains forbidden characters: / \\ : * ? \" < > | or control " "characters"
                ));
            }

            /// 规则 3：保留名称验证
            if (!request.ValidateReservedNames()) {
                Logger::Warn(log_context) << "Folder name is a reserved name: " << request.name;
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidFilename,
                    "Folder name cannot be reserved name \".\" or \"..\""
                ));
            }

            /// 规则 4：隐藏文件夹验证
            if (!request.ValidateNotHidden()) {
                Logger::Warn(log_context) << "Folder name starts with a dot (hidden folder): " << request.name;
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidFilename, "Folder name cannot start with \".\"")
                );
            }

            /// 规则 5：字符集验证（合法 UTF-8，且不含控制字符）
            if (!request.ValidateCharset()) {
                Logger::Warn(log_context) << "Folder name contains invalid UTF-8 or control characters: "
                                          << request.name;
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidFilename,
                    "Folder name must be valid UTF-8 and cannot contain control characters"
                ));
            }

            Logger::Debug(log_context) << "Request parameter validation passed";
            return request;
        }

    private:
        /// 验证长度 (1-255 字符)
        [[nodiscard]]
        auto ValidateLength() const -> bool {
            return name.length() >= 1 && name.length() <= 255;
        }

        /// 验证禁止字符 (/ \ : * ? " < > | 及控制字符 0x00-0x1F)
        [[nodiscard]]
        auto ValidateForbiddenChars() const -> bool {
            return !::disk::utils::HasForbiddenDriveItemChars(name);
        }

        /// 验证保留名称 (. 和 ..)
        [[nodiscard]]
        auto ValidateReservedNames() const -> bool {
            return name != "." && name != "..";
        }

        /// 验证不以点开头（非隐藏文件夹）
        [[nodiscard]]
        auto ValidateNotHidden() const -> bool {
            return name.empty() || name[0] != '.';
        }

        /// 验证字符集（合法 UTF-8，且不含控制字符）
        [[nodiscard]]
        auto ValidateCharset() const -> bool {
            return ::disk::utils::IsValidUtf8WithoutControlChars(name);
        }
    };

    /**
     * @brief 重命名文件夹请求 DTO
     */
    struct RenameFolderRequest : DtoBase<RenameFolderRequest> {
        uint64_t folder_id{ 0 };
        std::string new_name;

        [[nodiscard]]
        static auto FromPathAndRequest(
            const std::string& folder_id_str,
            const drogon::HttpRequestPtr& req,
            disk::utils::LogContext log_context = {}
        ) -> Result<RenameFolderRequest> {
            Logger::Debug(log_context) << "Start parsing rename folder request parameters";
            if (folder_id_str.empty()) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "Missing required parameter: folder_id")
                );
            }
            if (folder_id_str[0] == '-') {
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidParameter,
                    "Parameter 'folder_id' must be a positive integer"
                ));
            }

            uint64_t folder_id = 0;
            try {
                size_t pos = 0;
                folder_id = std::stoull(folder_id_str, &pos);
                if (pos != folder_id_str.length() || folder_id == 0) {
                    return std::unexpected(ErrorInfo(
                        ErrorCode::InvalidParameter,
                        "Parameter 'folder_id' must be a positive integer"
                    ));
                }
            } catch (const std::exception&) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidParameter, "Parameter 'folder_id' invalid format")
                );
            }

            auto json_result = RequireJsonBody(req);
            if (!json_result) {
                return std::unexpected(json_result.error());
            }
            const auto& json = *json_result.value();

            auto new_name_result = RequireString(json, "new_name");
            if (!new_name_result) {
                return std::unexpected(new_name_result.error());
            }

            RenameFolderRequest request;
            request.folder_id = folder_id;
            request.new_name = std::move(*new_name_result);
            request.new_name = folder_dto_detail::TrimAsciiSpaces(std::move(request.new_name));

            if (!request.ValidateLength()) {
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "Folder name length must be between 1-255 characters"
                ));
            }
            if (!request.ValidateForbiddenChars()) {
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidFilename,
                    "Folder name contains forbidden characters: / \\ : * ? \" < > | or control characters"
                ));
            }
            if (!request.ValidateReservedNames()) {
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidFilename,
                    "Folder name cannot be reserved name \".\" or \"..\""
                ));
            }
            if (!request.ValidateNotHidden()) {
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidFilename, "Folder name cannot start with \".\"")
                );
            }
            if (!request.ValidateCharset()) {
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidFilename,
                    "Folder name must be valid UTF-8 and cannot contain control characters"
                ));
            }

            return request;
        }

    private:
        [[nodiscard]] auto ValidateLength() const -> bool {
            return new_name.length() >= 1 && new_name.length() <= 255;
        }

        [[nodiscard]] auto ValidateForbiddenChars() const -> bool {
            return !::disk::utils::HasForbiddenDriveItemChars(new_name);
        }

        [[nodiscard]] auto ValidateReservedNames() const -> bool {
            return new_name != "." && new_name != "..";
        }

        [[nodiscard]] auto ValidateNotHidden() const -> bool {
            return new_name.empty() || new_name[0] != '.';
        }

        [[nodiscard]] auto ValidateCharset() const -> bool {
            return ::disk::utils::IsValidUtf8WithoutControlChars(new_name);
        }
    };

    /// ==================== Response DTOs ====================

    /**
     * @brief 创建文件夹响应 DTO
     *
     * @details
     * 包含新建文件夹的基本信息。
     */
    struct CreateFolderResponse : DtoBase<CreateFolderResponse> {
        uint64_t id;
        std::string name;
        uint64_t parent_id;
        std::string path;
        std::string created_at;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "id", id);
            SetField(json, "name", name);
            SetField(json, "parent_id", parent_id);
            SetField(json, "path", path);
            SetField(json, "created_at", created_at);
            return json;
        }
    };

    struct RenameFolderResponse : DtoBase<RenameFolderResponse> {
        uint64_t id{ 0 };
        std::string name;
        std::string path;
        std::string updated_at;

        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "id", id);
            SetField(json, "name", name);
            SetField(json, "path", path);
            SetField(json, "updated_at", updated_at);
            return json;
        }
    };

    /// ==================== Folder Tree DTOs ====================

    /**
     * @brief 获取文件夹树请求 DTO
     *
     * @details
     * 验证规则：
     * - parent_id: 默认 0（根目录），必须 >= 0
     * - depth: 默认 -1（无限深度），必须 >= -1
     *
     * 从 URL 查询参数解析：parent_id, depth
     */
    struct FolderTreeRequest : DtoBase<FolderTreeRequest> {
        uint64_t parent_id{ 0 };
        int depth{ -1 };

        /// 从 HTTP 请求查询参数解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(
            const drogon::HttpRequestPtr& req,
            disk::utils::LogContext log_context = {}
        ) -> Result<FolderTreeRequest> {
            Logger::Debug(log_context) << "Start parsing folder tree request parameters";

            FolderTreeRequest request;

            /// 解析可选参数 parent_id
            auto parent_id_result = QueryUInt64(req, "parent_id");
            if (!parent_id_result) {
                return std::unexpected(parent_id_result.error());
            }
            if (parent_id_result->has_value()) {
                request.parent_id = **parent_id_result;
            }

            /// 解析可选参数 depth（允许 -1，不能用 QueryPositiveInt）
            auto depth_str = req->getParameter("depth");
            if (!depth_str.empty()) {
                try {
                    size_t pos = 0;
                    auto value = std::stoi(depth_str, &pos);
                    if (pos != depth_str.length()) {
                        Logger::Warn(log_context) << "Parameter 'depth' invalid format: " << depth_str;
                        return std::unexpected(ErrorInfo(
                            ErrorCode::ValidationFailed,
                            "Parameter 'depth' invalid format"
                        ));
                    }
                    request.depth = value;
                } catch (const std::exception& e) {
                    Logger::Warn(log_context) << "Parameter 'depth' invalid format: " << depth_str;
                    return std::unexpected(
                        ErrorInfo(ErrorCode::ValidationFailed, "Parameter 'depth' invalid format")
                    );
                }
            }

            /// 验证 depth >= -1
            if (request.depth < -1) {
                Logger::Warn(log_context) << "Parameter 'depth' cannot be less than -1: " << request.depth;
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "Parameter 'depth' cannot be less than -1"
                ));
            }

            Logger::Debug(log_context) << "Parsed folder tree request: parent_id=" << request.parent_id
                                       << ", depth=" << request.depth;

            return request;
        }
    };

    /**
     * @brief 文件夹节点数据（内部结构）
     *
     * @details
     * 用于存储从数据库查询的文件夹行数据。
     */
    struct FolderNodeData {
        uint64_t id;
        std::string name;
        uint64_t parent_id;
    };

    /**
     * @brief 文件夹树节点响应 DTO
     *
     * @details
     * 用于构建递归的文件夹树结构。
     * 包含文件夹基本信息和子节点列表。
     */
    struct FolderTreeNode : DtoBase<FolderTreeNode> {
        uint64_t id;
        std::string name;
        std::vector<FolderTreeNode> children;

        /// 转换为 JSON（递归序列化）
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "id", id);
            SetField(json, "name", name);
            SetArray(json, "children", children);
            return json;
        }
    };

    /// ==================== Breadcrumb DTOs ====================

    /**
     * @brief 面包屑导航项
     *
     * @details
     * 表示路径中的单个文件夹节点。
     */
    struct BreadcrumbItem : DtoBase<BreadcrumbItem> {
        uint64_t id;
        std::string name;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "id", id);
            SetField(json, "name", name);
            return json;
        }
    };

    /**
     * @brief 面包屑导航响应 DTO
     *
     * @details
     * 包含从根目录到当前文件夹的完整路径。
     */
    struct BreadcrumbResponse : DtoBase<BreadcrumbResponse> {
        std::vector<BreadcrumbItem> path;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetArray(json, "path", path);
            return json;
        }
    };

} ///< namespace disk::folder
