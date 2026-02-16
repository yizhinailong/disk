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

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <regex>
#include <string>
#include <vector>

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
    struct UpdateProfileRequest {
        std::optional<std::string> nickname;
        std::optional<std::string> avatar;

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<UpdateProfileRequest> {
            LOG_DEBUG << "开始解析更新用户资料请求参数";

            auto json_ptr = req->getJsonObject();
            if (!json_ptr) {
                LOG_WARN << "请求体不是有效的 JSON";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "请求体不是有效的 JSON"));
            }

            const auto& json = *json_ptr;
            UpdateProfileRequest request;

            // 解析 nickname（可选）
            if (json.isMember("nickname")) {
                if (json["nickname"].isNull()) {
                    LOG_WARN << "参数 'nickname' 不能为 null";
                    return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'nickname' 不能为 null"));
                }
                if (!json["nickname"].isString()) {
                    LOG_WARN << "参数 'nickname' 类型错误: 期望字符串";
                    return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'nickname' 类型错误: 期望字符串"));
                }
                std::string nickname_value = json["nickname"].asString();
                nickname_value.erase(
                    nickname_value.begin(),
                    std::ranges::find_if(
                        nickname_value,
                        [](unsigned char ch) { return !std::isspace(ch); }
                    )
                );
                nickname_value.erase(
                    std::ranges::find_if(
                        nickname_value.rbegin(),
                        nickname_value.rend(),
                        [](unsigned char ch) { return !std::isspace(ch); }
                    ).base(),
                    nickname_value.end()
                );
                if (!nickname_value.empty()) {
                    request.nickname = nickname_value;
                }
            }

            // 解析 avatar（可选）
            if (json.isMember("avatar")) {
                if (json["avatar"].isNull()) {
                    LOG_WARN << "参数 'avatar' 不能为 null";
                    return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'avatar' 不能为 null"));
                }
                if (!json["avatar"].isString()) {
                    LOG_WARN << "参数 'avatar' 类型错误: 期望字符串";
                    return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'avatar' 类型错误: 期望字符串"));
                }
                std::string avatar_value = json["avatar"].asString();
                avatar_value.erase(
                    avatar_value.begin(),
                    std::ranges::find_if(
                        avatar_value,
                        [](unsigned char ch) { return !std::isspace(ch); }
                    )
                );
                avatar_value.erase(
                    std::ranges::find_if(
                        avatar_value.rbegin(),
                        avatar_value.rend(),
                        [](unsigned char ch) { return !std::isspace(ch); }
                    ).base(),
                    avatar_value.end()
                );
                if (!avatar_value.empty()) {
                    request.avatar = avatar_value;
                }
            }

            // 验证字段长度
            if (request.nickname.has_value() && !request.ValidateNickname()) {
                LOG_WARN << "昵称格式错误";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "昵称长度必须在1-64字符之间"));
            }

            if (request.avatar.has_value() && !request.ValidateAvatar()) {
                LOG_WARN << "头像格式错误";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "头像链接长度必须在1-512字符之间"));
            }

            // 至少提供一个字段
            if (!request.nickname.has_value() && !request.avatar.has_value()) {
                LOG_WARN << "至少需要提供一个字段";
                return std::unexpected(ErrorInfo(
                    ErrorCode::ValidationFailed,
                    "至少需要提供一个字段（nickname 或 avatar）"
                ));
            }

            LOG_DEBUG << "请求参数验证通过";

            return request;
        }

    private:
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

    /**
     * @brief 存储分类统计 DTO
     *
     * @details
     * 用于按文件类型分类统计存储空间使用情况。
     * 文件类型包括：document, image, video, audio, other
     */
    struct StorageCategory {
        std::string type; ///< 文件类型：document, image, video, audio, other
        uint64_t size;    ///< 该类型总大小（字节）
        uint32_t count;   ///< 该类型文件数量

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["type"] = type;
            json["size"] = static_cast<Json::UInt64>(size);
            json["count"] = count;
            return json;
        }
    };

    /**
     * @brief 存储空间统计响应 DTO
     *
     * @details
     * 返回用户存储空间使用情况，包括已用空间、总配额、
     * 使用百分比、文件/文件夹数量及分类统计。
     */
    struct StorageResponse {
        uint64_t used;                           ///< 已使用空间（字节）
        uint64_t quota;                          ///< 总配额（字节）
        double percentage;                       ///< 使用百分比（1位小数）
        uint32_t file_count;                     ///< 文件数量
        uint32_t folder_count;                   ///< 文件夹数量
        std::vector<StorageCategory> categories; ///< 分类统计（当前版本为空）

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["used"] = static_cast<Json::UInt64>(used);
            json["quota"] = static_cast<Json::UInt64>(quota);
            json["percentage"] = percentage;
            json["file_count"] = file_count;
            json["folder_count"] = folder_count;
            Json::Value categories_array(Json::arrayValue);
            for (const auto& cat : categories) {
                categories_array.append(cat.ToJson());
            }
            json["categories"] = categories_array;
            return json;
        }
    };

} // namespace disk::user
