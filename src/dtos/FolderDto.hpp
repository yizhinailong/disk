/**
 * @file FolderDto.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件夹模块数据传输对象
 * @version 0.1
 * @date 2026-02-14
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 本文件包含文件夹模块的所有数据传输对象（DTO）：
 * - CreateFolderRequest: 创建文件夹请求
 * - CreateFolderResponse: 创建文件夹响应
 *
 * DTO 用于在不同层（Controller、Service）之间传输数据，
 * 包含请求验证和响应序列化逻辑。
 */

#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include <drogon/HttpRequest.h>
#include <json/json.h>

#include "utils/ErrorCode.hpp"

namespace disk::folder {

    // ==================== Request DTOs ====================

    /**
     * @brief 创建文件夹请求 DTO
     *
     * @details
     * 验证规则：
     * - name: 1-255字符（去除首尾空格后）
     * - name: 禁止字符 / \ : * ? " < > | 及控制字符 (0x00-0x1F)
     * - name: 禁止保留名称 "." 和 ".."
     * - name: 禁止以 "." 开头（隐藏文件夹）
     * - name: 仅允许 ASCII 可打印字符 (0x20-0x7E)
     * - parent_id: 默认 0（根目录）
     */
    struct CreateFolderRequest {
        std::string name;
        uint64_t parent_id{ 0 };

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<CreateFolderRequest> {
            LOG_DEBUG << "Start parsing create folder request parameters";

            auto json_ptr = req->getJsonObject();
            if (!json_ptr) {
                LOG_WARN << "Request body is not valid JSON";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Request body is not valid JSON")
                );
            }

            const auto& json = *json_ptr;

            // 检查必填字段
            if (!json.isMember("name")) {
                LOG_WARN << "Missing required parameter: name";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Missing required parameter: name")
                );
            }

            // 检查字段类型
            if (!json["name"].isString()) {
                LOG_WARN << "Parameter 'name' type error: expected string";
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "Parameter 'name' type error: expected string"
                ));
            }

            CreateFolderRequest request;
            request.name = json["name"].asString();

            // 处理可选参数 parent_id
            if (json.isMember("parent_id")) {
                if (!json["parent_id"].isIntegral()) {
                    LOG_WARN << "Parameter 'parent_id' type error: expected integer";
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'parent_id' type error: expected integer"
                    ));
                }
                request.parent_id = json["parent_id"].asUInt64();
            }

            // Rule 6: 去除首尾空格
            request.TrimName();

            LOG_DEBUG << "Parsed create folder request: name=\"" << request.name
                      << "\", parent_id=" << request.parent_id;

            // Rule 1: 长度验证 (1-255)
            if (!request.ValidateLength()) {
                LOG_WARN << "Invalid folder name length: " << request.name.length();
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "Folder name length must be between 1-255 characters"
                ));
            }

            // Rule 2: 禁止字符验证
            if (!request.ValidateForbiddenChars()) {
                LOG_WARN << "Folder name contains forbidden characters: " << request.name;
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidFilename,
                    "Folder name contains forbidden characters: / \\ : * ? \" < > | or control "
                    "characters"
                ));
            }

            // Rule 3: 保留名称验证
            if (!request.ValidateReservedNames()) {
                LOG_WARN << "Folder name is a reserved name: " << request.name;
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidFilename,
                    "Folder name cannot be reserved name \".\" or \"..\""
                ));
            }

            // Rule 4: 隐藏文件夹验证
            if (!request.ValidateNotHidden()) {
                LOG_WARN << "Folder name starts with a dot (hidden folder): " << request.name;
                return std::unexpected(
                    ErrorInfo(ErrorCode::InvalidFilename, "Folder name cannot start with \".\"")
                );
            }

            // Rule 5: 字符集验证 (仅 ASCII 可打印字符)
            if (!request.ValidateCharset()) {
                LOG_WARN << "Folder name contains non-ASCII characters: " << request.name;
                return std::unexpected(ErrorInfo(
                    ErrorCode::InvalidFilename,
                    "Folder name only allows ASCII printable characters"
                ));
            }

            LOG_DEBUG << "Request parameter validation passed";
            return request;
        }

    private:
        /// 去除首尾空格
        auto TrimName() -> void {
            // 去除首尾空格
            auto start = name.find_first_not_of(' ');
            if (start == std::string::npos) {
                name.clear();
                return;
            }
            auto end = name.find_last_not_of(' ');
            name = name.substr(start, end - start + 1);
        }

        /// 验证长度 (1-255 字符)
        [[nodiscard]]
        auto ValidateLength() const -> bool {
            return name.length() >= 1 && name.length() <= 255;
        }

        /// 验证禁止字符 (/ \ : * ? " < > | 及控制字符 0x00-0x1F)
        [[nodiscard]]
        auto ValidateForbiddenChars() const -> bool {
            static const char forbidden_chars[] = "/\\:*?\"<>|";
            for (char c : name) {
                // 检查控制字符 (0x00-0x1F)
                if (static_cast<unsigned char>(c) <= 0x1F) {
                    return false;
                }
                // 检查文件系统保留字符
                for (char fc : forbidden_chars) {
                    if (c == fc) {
                        return false;
                    }
                }
            }
            return true;
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

        /// 验证字符集（仅 ASCII 可打印字符 0x20-0x7E）
        [[nodiscard]]
        auto ValidateCharset() const -> bool {
            for (char c : name) {
                // ASCII 可打印字符范围: 0x20 (空格) 到 0x7E (~)
                if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) {
                    return false;
                }
            }
            return true;
        }
    };

    // ==================== Response DTOs ====================

    /**
     * @brief 创建文件夹响应 DTO
     *
     * @details
     * 包含新建文件夹的基本信息。
     */
    struct CreateFolderResponse {
        uint64_t id;
        std::string name;
        uint64_t parent_id;
        std::string path;
        std::string created_at;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["id"] = static_cast<Json::UInt64>(id);
            json["name"] = name;
            json["parent_id"] = static_cast<Json::UInt64>(parent_id);
            json["path"] = path;
            json["created_at"] = created_at;
            return json;
        }
    };

    // ==================== Folder Tree DTOs ====================

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
    struct FolderTreeRequest {
        uint64_t parent_id{ 0 };
        int depth{ -1 };

        /// 从 HTTP 请求查询参数解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<FolderTreeRequest> {
            LOG_DEBUG << "Start parsing folder tree request parameters";

            FolderTreeRequest request;

            // 解析可选参数 parent_id
            auto parent_id_str = req->getParameter("parent_id");
            if (!parent_id_str.empty()) {
                try {
                    size_t pos = 0;
                    auto value = std::stoull(parent_id_str, &pos);
                    if (pos != parent_id_str.length()) {
                        LOG_WARN << "Parameter 'parent_id' invalid format: " << parent_id_str;
                        return std::unexpected(ErrorInfo(
                            ErrorCode::ValidationFailed,
                            "Parameter 'parent_id' invalid format"
                        ));
                    }
                    request.parent_id = value;
                } catch (const std::exception& e) {
                    LOG_WARN << "Parameter 'parent_id' invalid format: " << parent_id_str;
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'parent_id' invalid format"
                    ));
                }
            }

            // 解析可选参数 depth
            auto depth_str = req->getParameter("depth");
            if (!depth_str.empty()) {
                try {
                    size_t pos = 0;
                    auto value = std::stoi(depth_str, &pos);
                    if (pos != depth_str.length()) {
                        LOG_WARN << "Parameter 'depth' invalid format: " << depth_str;
                        return std::unexpected(ErrorInfo(
                            ErrorCode::ValidationFailed,
                            "Parameter 'depth' invalid format"
                        ));
                    }
                    request.depth = value;
                } catch (const std::exception& e) {
                    LOG_WARN << "Parameter 'depth' invalid format: " << depth_str;
                    return std::unexpected(
                        ErrorInfo(ErrorCode::ValidationFailed, "Parameter 'depth' invalid format")
                    );
                }
            }

            // 验证 depth >= -1
            if (request.depth < -1) {
                LOG_WARN << "Parameter 'depth' cannot be less than -1: " << request.depth;
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "Parameter 'depth' cannot be less than -1"
                ));
            }

            LOG_DEBUG << "Parsed folder tree request: parent_id=" << request.parent_id
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
    struct FolderTreeNode {
        uint64_t id;
        std::string name;
        std::vector<FolderTreeNode> children;

        /// 转换为 JSON（递归序列化）
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["id"] = static_cast<Json::UInt64>(id);
            json["name"] = name;

            Json::Value children_array(Json::arrayValue);
            for (const auto& child : children) {
                children_array.append(child.ToJson());
            }
            json["children"] = children_array;

            return json;
        }
    };

    // ==================== Breadcrumb DTOs ====================

    /**
     * @brief 面包屑导航项
     *
     * @details
     * 表示路径中的单个文件夹节点。
     */
    struct BreadcrumbItem {
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
     * @brief 面包屑导航响应 DTO
     *
     * @details
     * 包含从根目录到当前文件夹的完整路径。
     */
    struct BreadcrumbResponse {
        std::vector<BreadcrumbItem> path;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            Json::Value path_array(Json::arrayValue);
            for (const auto& item : path) {
                path_array.append(item.ToJson());
            }
            json["path"] = path_array;
            return json;
        }
    };

} // namespace disk::folder
