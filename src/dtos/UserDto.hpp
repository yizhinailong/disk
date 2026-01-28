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
#include <string>

#include <json/json.h>

namespace disk::user {

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
