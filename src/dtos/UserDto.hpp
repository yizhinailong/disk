/**
 * @file UserDto.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 用户模块数据传输对象（Data Transfer Objects）
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 本文件包含用户模块的所有数据传输对象（DTO）：
 * - ChangePasswordRequest: 修改密码请求
 * - UpdateProfileRequest: 更新用户资料请求
 * - UserProfileResponse: 用户信息响应
 * - StorageResponse: 存储空间统计响应
 */

#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include <drogon/HttpRequest.h>
#include <json/json.h>

#include "utils/DtoBase.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::user {

    /// ==================== Request DTOs ====================

    /**
     * @brief 修改密码请求 DTO
     *
     * @details
     * 验证规则：
     * - old_password: 必填，旧密码（字符串）
     * - new_password: 必填，新密码（字符串，8-64字符，需含大小写字母和数字）
     * - new_password != old_password
     */
    struct ChangePasswordRequest : DtoBase<ChangePasswordRequest> {
        std::string old_password;
        std::string new_password;

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req)
            -> Result<ChangePasswordRequest> {
            Logger::Debug() << "Start parsing change password request parameters";

            auto json_result = RequireJsonBody(req);
            if (!json_result) return std::unexpected(json_result.error());
            const auto& json = *json_result.value();

            auto old_pwd = RequireString(json, "old_password");
            if (!old_pwd) return std::unexpected(old_pwd.error());

            auto new_pwd = RequireString(json, "new_password");
            if (!new_pwd) return std::unexpected(new_pwd.error());

            ChangePasswordRequest request;
            request.old_password = std::move(*old_pwd);
            request.new_password = std::move(*new_pwd);

            Logger::Debug() << "Parsed change password request";

            if (!request.ValidateNewPassword()) {
                Logger::Warn() << "New password format error";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "New password format error")
                );
            }

            if (request.old_password == request.new_password) {
                Logger::Warn() << "New password cannot be the same as old password";
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "New password cannot be the same as old password"
                ));
            }

            Logger::Debug() << "Request parameters validated";

            return request;
        }

    private:
        /// 验证新密码
        [[nodiscard]]
        auto ValidateNewPassword() const -> bool {
            if (new_password.length() < 8 || new_password.length() > 64) {
                return false;
            }
            static const std::regex password_regex(
                "^(?=.*[a-z])(?=.*[A-Z])(?=.*\\d)[a-zA-Z\\d]{8,64}$"
            );
            return std::regex_match(new_password, password_regex);
        }
    };

    /**
     * @brief 更新用户资料请求 DTO
     *
     * @details
     * 验证规则：
     * - nickname: 可选，1-64字符（去除首尾空格后）
     * - avatar: 可选，1-512字符（去除首尾空格后）
     * - 至少提供一个字段
     * - 显式 JSON null 值视为无效
     */
    struct UpdateProfileRequest : DtoBase<UpdateProfileRequest> {
        std::optional<std::string> nickname;
        std::optional<std::string> avatar;

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<UpdateProfileRequest> {
            Logger::Debug() << "Start parsing update profile request parameters";

            auto json_result = RequireJsonBody(req);
            if (!json_result) return std::unexpected(json_result.error());
            const auto& json = *json_result.value();

            UpdateProfileRequest request;

            /// 解析 nickname（可选，显式 null 无效）
            if (json.isMember("nickname")) {
                if (json["nickname"].isNull()) {
                    Logger::Warn() << "Parameter 'nickname' cannot be null";
                    return std::unexpected(ErrorInfo(
                        ErrorCode::ValidationFailed,
                        "Parameter 'nickname' cannot be null"
                    ));
                }
                auto nickname_result = RequireString(json, "nickname");
                if (!nickname_result) return std::unexpected(nickname_result.error());
                auto trimmed = TrimWhitespace(*nickname_result);
                if (!trimmed.empty()) {
                    request.nickname = std::move(trimmed);
                }
            }

            /// 解析 avatar（可选，显式 null 无效）
            if (json.isMember("avatar")) {
                if (json["avatar"].isNull()) {
                    Logger::Warn() << "Parameter 'avatar' cannot be null";
                    return std::unexpected(
                        ErrorInfo(ErrorCode::ValidationFailed, "Parameter 'avatar' cannot be null")
                    );
                }
                auto avatar_result = RequireString(json, "avatar");
                if (!avatar_result) return std::unexpected(avatar_result.error());
                auto trimmed = TrimWhitespace(*avatar_result);
                if (!trimmed.empty()) {
                    request.avatar = std::move(trimmed);
                }
            }

            /// 验证字段长度
            if (request.nickname.has_value() && !request.ValidateNickname()) {
                Logger::Warn() << "Nickname format error";
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "Nickname length must be between 1-64 characters"
                ));
            }

            if (request.avatar.has_value() && !request.ValidateAvatar()) {
                Logger::Warn() << "Avatar format error";
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "Avatar URL length must be between 1-512 characters"
                ));
            }

            /// 至少提供一个字段
            if (!request.nickname.has_value() && !request.avatar.has_value()) {
                Logger::Warn() << "At least one field must be provided";
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "At least one field must be provided (nickname or avatar)"
                ));
            }

            Logger::Debug() << "Request parameters validated";

            return request;
        }

    private:
        [[nodiscard]]
        static auto TrimWhitespace(std::string str) -> std::string {
            str.erase(
                str.begin(),
                std::ranges::find_if(str, [](unsigned char ch) { return !std::isspace(ch); })
            );
            str.erase(
                std::ranges::find_if(
                    str.rbegin(),
                    str.rend(),
                    [](unsigned char ch) { return !std::isspace(ch); }
                ).base(),
                str.end()
            );
            return str;
        }

        /// 验证昵称
        [[nodiscard]]
        auto ValidateNickname() const -> bool {
            const auto& value = nickname.value();
            return value.length() >= 1 && value.length() <= 64;
        }

        /// 验证头像
        [[nodiscard]]
        auto ValidateAvatar() const -> bool {
            const auto& value = avatar.value();
            return value.length() >= 1 && value.length() <= 512;
        }
    };

    /// ==================== Response DTOs ====================

    /**
     * @brief 用户信息响应 DTO
     *
     * @details
     * 包含用户的完整信息，用于用户个人资料相关的响应。
     * 可空字段返回空字符串而非 JSON null。
     */
    struct UserProfileResponse : DtoBase<UserProfileResponse> {
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
            SetField(json, "id", id);
            SetField(json, "username", username);
            SetField(json, "email", email);
            SetField(json, "nickname", nickname);
            SetField(json, "avatar", avatar);
            SetField(json, "storage_used", storage_used);
            SetField(json, "storage_quota", storage_quota);
            SetField(json, "file_count", file_count);
            SetField(json, "folder_count", folder_count);
            SetField(json, "created_at", created_at);
            SetField(json, "updated_at", updated_at);
            return json;
        }
    };

    /**
     * @brief 存储空间统计响应 DTO
     *
     * @details
     * 返回用户存储空间使用情况，包括已用空间、总配额、
     * 使用百分比、文件/文件夹数量。
     */
    struct StorageResponse : DtoBase<StorageResponse> {
        uint64_t used;       ///< 已使用空间（字节）
        uint64_t quota;      ///< 总配额（字节）
        double percentage;   ///< 使用百分比（1位小数）
        uint32_t file_count; ///< 文件数量
        uint32_t folder_count; ///< 文件夹数量

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "used", used);
            SetField(json, "quota", quota);
            SetField(json, "percentage", percentage);
            SetField(json, "file_count", file_count);
            SetField(json, "folder_count", folder_count);
            return json;
        }
    };

} ///< namespace disk::user
