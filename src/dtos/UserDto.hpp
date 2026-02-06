/**
 * @file UserDto.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 用户模块数据传输对象（Data Transfer Objects）
 * @version 0.1
 * @date 2026-01-28
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 本文件包含用户模块的所有数据传输对象（DTO）：
 * - UserProfileResponse: 用户信息响应
 */

#pragma once

#include <cstdint>
#include <regex>
#include <string>

#include <drogon/HttpRequest.h>
#include <json/json.h>

#include "utils/ErrorCode.hpp"

namespace disk::user {

    // ==================== Request DTOs ====================

    /**
     * @brief 修改密码请求 DTO
     *
     * @details
     * 验证规则：
     * - old_password: 必填，旧密码（字符串）
     * - new_password: 必填，新密码（字符串，8-64字符，需含大小写字母和数字）
     * - new_password != old_password
     */
    struct ChangePasswordRequest {
        std::string old_password;
        std::string new_password;

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<ChangePasswordRequest> {
            LOG_DEBUG << "开始解析修改密码请求参数";

            auto json_ptr = req->getJsonObject();
            if (!json_ptr) {
                LOG_WARN << "请求体不是有效的 JSON";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "请求体不是有效的 JSON"));
            }

            const auto& json = *json_ptr;

            // 检查必填字段
            if (!json.isMember("old_password")) {
                LOG_WARN << "缺少必需参数: old_password";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: old_password"));
            }
            if (!json.isMember("new_password")) {
                LOG_WARN << "缺少必需参数: new_password";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: new_password"));
            }

            // 检查字段类型
            if (!json["old_password"].isString()) {
                LOG_WARN << "参数 'old_password' 类型错误: 期望字符串";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'old_password' 类型错误: 期望字符串"));
            }
            if (!json["new_password"].isString()) {
                LOG_WARN << "参数 'new_password' 类型错误: 期望字符串";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'new_password' 类型错误: 期望字符串"));
            }

            ChangePasswordRequest request;
            request.old_password = json["old_password"].asString();
            request.new_password = json["new_password"].asString();

            LOG_DEBUG << "解析到修改密码请求";

            if (!request.ValidateNewPassword()) {
                LOG_WARN << "新密码格式错误";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "新密码格式错误"));
            }

            if (request.old_password == request.new_password) {
                LOG_WARN << "新密码不能与旧密码相同";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "新密码不能与旧密码相同"));
            }

            LOG_DEBUG << "请求参数验证通过";

            return request;
        }

    private:
        /// 验证新密码
        [[nodiscard]]
        auto ValidateNewPassword() const -> bool {
            if (new_password.length() < 8 || new_password.length() > 64) {
                return false;
            }
            static const std::regex password_regex("^(?=.*[a-z])(?=.*[A-Z])(?=.*\\d)[a-zA-Z\\d]{8,64}$");
            return std::regex_match(new_password, password_regex);
        }
    };

    // ==================== Response DTOs ====================

    /**
     * @brief 用户信息响应 DTO
     *
     * @details
     * 包含用户的完整信息，用于用户个人资料相关的响应。
     * 可空字段返回空字符串而非 JSON null。
     */
    struct UserProfileResponse {
        uint64_t id;
        std::string username;
        std::string email;
        std::string nickname;
        std::string avatar;
        uint64_t storage_used;
        uint64_t storage_quota;
        uint32_t file_count;
        uint32_t folder_count;
        std::string created_at;
        std::string updated_at;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["id"] = static_cast<Json::UInt64>(id);
            json["username"] = username;
            json["email"] = email;
            json["nickname"] = nickname;
            json["avatar"] = avatar;
            json["storage_used"] = static_cast<Json::UInt64>(storage_used);
            json["storage_quota"] = static_cast<Json::UInt64>(storage_quota);
            json["file_count"] = file_count;
            json["folder_count"] = folder_count;
            json["created_at"] = created_at;
            json["updated_at"] = updated_at;
            return json;
        }
    };

} // namespace disk::user
