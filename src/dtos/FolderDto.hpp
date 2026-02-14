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
            LOG_DEBUG << "开始解析创建文件夹请求参数";

            auto json_ptr = req->getJsonObject();
            if (!json_ptr) {
                LOG_WARN << "请求体不是有效的 JSON";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "请求体不是有效的 JSON"));
            }

            const auto& json = *json_ptr;

            // 检查必填字段
            if (!json.isMember("name")) {
                LOG_WARN << "缺少必需参数: name";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: name"));
            }

            // 检查字段类型
            if (!json["name"].isString()) {
                LOG_WARN << "参数 'name' 类型错误: 期望字符串";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'name' 类型错误: 期望字符串"));
            }

            CreateFolderRequest request;
            request.name = json["name"].asString();

            // 处理可选参数 parent_id
            if (json.isMember("parent_id")) {
                if (!json["parent_id"].isIntegral()) {
                    LOG_WARN << "参数 'parent_id' 类型错误: 期望整数";
                    return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'parent_id' 类型错误: 期望整数"));
                }
                request.parent_id = json["parent_id"].asUInt64();
            }

            // Rule 6: 去除首尾空格
            request.TrimName();

            LOG_DEBUG << "解析到创建文件夹请求: name=\"" << request.name << "\", parent_id=" << request.parent_id;

            // Rule 1: 长度验证 (1-255)
            if (!request.ValidateLength()) {
                LOG_WARN << "文件夹名称长度无效: " << request.name.length();
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "文件夹名称长度必须在 1-255 字符之间"));
            }

            // Rule 2: 禁止字符验证
            if (!request.ValidateForbiddenChars()) {
                LOG_WARN << "文件夹名称包含禁止字符: " << request.name;
                return std::unexpected(ErrorInfo(ErrorCode::InvalidFilename, "文件夹名称包含禁止字符：/ \\ : * ? \" < > | 或控制字符"));
            }

            // Rule 3: 保留名称验证
            if (!request.ValidateReservedNames()) {
                LOG_WARN << "文件夹名称为保留名称: " << request.name;
                return std::unexpected(ErrorInfo(ErrorCode::InvalidFilename, "文件夹名称不能为保留名称 \".\" 或 \"..\""));
            }

            // Rule 4: 隐藏文件夹验证
            if (!request.ValidateNotHidden()) {
                LOG_WARN << "文件夹名称以点开头（隐藏文件夹）: " << request.name;
                return std::unexpected(ErrorInfo(ErrorCode::InvalidFilename, "文件夹名称不能以 \".\" 开头"));
            }

            // Rule 5: 字符集验证 (仅 ASCII 可打印字符)
            if (!request.ValidateCharset()) {
                LOG_WARN << "文件夹名称包含非 ASCII 字符: " << request.name;
                return std::unexpected(ErrorInfo(ErrorCode::InvalidFilename, "文件夹名称仅允许 ASCII 可打印字符"));
            }

            LOG_DEBUG << "请求参数验证通过";
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

} // namespace disk::folder
