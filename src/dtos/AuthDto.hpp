/**
 * @file AuthDto.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 认证模块数据传输对象（Data Transfer Objects）
 * @version 0.1
 * @date 2026-01-21
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 本文件包含认证模块的所有数据传输对象（DTO）：
 * - RegisterRequest: 用户注册请求
 * - LoginRequest: 用户登录请求
 * - RefreshTokenRequest: 刷新令牌请求
 * - RegisterResponse: 用户注册响应
 * - LoginResponse: 用户登录响应
 * - RefreshTokenResponse: 刷新令牌响应
 *
 * DTO 用于在不同层（Controller、Service）之间传输数据，
 * 包含请求验证和响应序列化逻辑。
 */

#pragma once

#include <regex>
#include <string>

#include <drogon/HttpRequest.h>
#include <json/json.h>

#include "utils/ErrorCode.hpp"

namespace disk::auth {

    // ==================== Request DTOs ====================

    /**
     * @brief 用户注册请求 DTO
     *
     * @details
     * 验证规则：
     * - username: 4-32字符，字母数字下划线
     * - email: 有效邮箱格式
     * - password: 8-64字符，仅含字母和数字，且需同时包含大小写字母和数字
     */
    struct RegisterRequest {
        std::string username;
        std::string email;
        std::string password;

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<RegisterRequest> {
            LOG_DEBUG << "开始解析注册请求参数";

            auto json_ptr = req->getJsonObject();
            if (!json_ptr) {
                LOG_WARN << "请求体不是有效的 JSON";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "请求体不是有效的 JSON"));
            }

            const auto& json = *json_ptr;

            // 检查必填字段
            if (!json.isMember("username")) {
                LOG_WARN << "缺少必需参数: username";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: username"));
            }
            if (!json.isMember("email")) {
                LOG_WARN << "缺少必需参数: email";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: email"));
            }
            if (!json.isMember("password")) {
                LOG_WARN << "缺少必需参数: password";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: password"));
            }

            // 检查字段类型
            if (!json["username"].isString()) {
                LOG_WARN << "参数 'username' 类型错误: 期望字符串";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'username' 类型错误: 期望字符串"));
            }
            if (!json["email"].isString()) {
                LOG_WARN << "参数 'email' 类型错误: 期望字符串";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'email' 类型错误: 期望字符串"));
            }
            if (!json["password"].isString()) {
                LOG_WARN << "参数 'password' 类型错误: 期望字符串";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'password' 类型错误: 期望字符串"));
            }

            RegisterRequest request;
            request.username = json["username"].asString();
            request.email = json["email"].asString();
            request.password = json["password"].asString();

            LOG_DEBUG << "解析到注册请求: " << request.username << " <" << request.email << ">";

            if (!request.ValidateUsername()) {
                LOG_WARN << "用户名格式错误: " << request.username;
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "用户名格式错误"));
            }
            if (!request.ValidateEmail()) {
                LOG_WARN << "邮箱格式错误: " << request.email;
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "邮箱格式错误"));
            }
            if (!request.ValidatePassword()) {
                LOG_WARN << "密码格式错误: " << request.username;
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "密码格式错误"));
            }
            LOG_DEBUG << "请求参数验证通过";

            return request;
        }

    private:
        /// 验证用户名
        [[nodiscard]]
        auto ValidateUsername() const -> bool {
            if (username.length() < 4 || username.length() > 32) {
                return false;
            }
            static const std::regex username_regex("^[a-zA-Z0-9_]+$");
            return std::regex_match(username, username_regex);
        }

        /// 验证邮箱
        [[nodiscard]]
        auto ValidateEmail() const -> bool {
            static const std::regex email_regex(R"(^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$)");
            return std::regex_match(email, email_regex);
        }

        /// 验证密码
        [[nodiscard]]
        auto ValidatePassword() const -> bool {
            if (password.length() < 8 || password.length() > 64) {
                return false;
            }
            static const std::regex password_regex("^(?=.*[a-z])(?=.*[A-Z])(?=.*\\d)[a-zA-Z\\d]{8,64}$");
            return std::regex_match(password, password_regex);
        }
    };

    /**
     * @brief 用户登录请求 DTO
     *
     * @details
     * 验证规则：
     * - account: 用户名或邮箱（必填，字符串）
     * - password: 密码（必填，字符串）
     */
    struct LoginRequest {
        std::string account;
        std::string password;

        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<LoginRequest> {
            LOG_DEBUG << "开始解析登录请求参数";

            auto json_ptr = req->getJsonObject();
            if (!json_ptr) {
                LOG_WARN << "请求体不是有效的 JSON";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "请求体不是有效的 JSON"));
            }

            const auto& json = *json_ptr;

            // 检查必填字段
            if (!json.isMember("account")) {
                LOG_WARN << "缺少必需参数: account";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: account"));
            }
            if (!json.isMember("password")) {
                LOG_WARN << "缺少必需参数: password";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: password"));
            }

            // 检查字段类型
            if (!json["account"].isString()) {
                LOG_WARN << "参数 'account' 类型错误: 期望字符串";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'account' 类型错误: 期望字符串"));
            }
            if (!json["password"].isString()) {
                LOG_WARN << "参数 'password' 类型错误: 期望字符串";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'password' 类型错误: 期望字符串"));
            }

            LoginRequest request;
            request.account = json["account"].asString();
            request.password = json["password"].asString();

            LOG_DEBUG << "解析到登录请求: " << request.account;

            return request;
        }
    };

    /**
     * @brief 刷新令牌请求 DTO
     *
     * @details
     * 验证规则：
     * - refresh_token: 必填，有效的 JWT 字符串
     */
    struct RefreshTokenRequest {
        std::string refresh_token;

        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<RefreshTokenRequest> {
            LOG_DEBUG << "开始解析刷新令牌请求参数";

            auto json_ptr = req->getJsonObject();
            if (!json_ptr) {
                LOG_WARN << "请求体不是有效的 JSON";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "请求体不是有效的 JSON"));
            }

            const auto& json = *json_ptr;

            // 检查必填字段
            if (!json.isMember("refresh_token")) {
                LOG_WARN << "缺少必需参数: refresh_token";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: refresh_token"));
            }

            // 检查字段类型
            if (!json["refresh_token"].isString()) {
                LOG_WARN << "参数 'refresh_token' 类型错误: 期望字符串";
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'refresh_token' 类型错误: 期望字符串"));
            }

            RefreshTokenRequest request;
            request.refresh_token = json["refresh_token"].asString();

            LOG_DEBUG << "解析到刷新令牌请求";

            return request;
        }
    };

    // ==================== Response DTOs ====================

    /**
     * @brief 用户注册响应 DTO
     *
     * @details
     * 包含用户的基本信息和存储配额。
     */
    struct RegisterResponse {
        uint64_t id;
        std::string username;
        std::string email;
        std::string nickname;
        uint64_t storage_quota;
        uint64_t storage_used;
        std::string created_at;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["id"] = static_cast<Json::UInt64>(id);
            json["username"] = username;
            json["email"] = email;
            json["nickname"] = nickname;
            json["storage_quota"] = static_cast<Json::UInt64>(storage_quota);
            json["storage_used"] = static_cast<Json::UInt64>(storage_used);
            json["created_at"] = created_at;
            return json;
        }
    };

    /**
     * @brief 用户登录响应 DTO
     *
     * @details
     * 包含访问令牌、刷新令牌和用户信息。
     */
    struct LoginResponse {
        std::string access_token;
        std::string refresh_token;
        std::string token_type;
        int expires_in;
        RegisterResponse user;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["access_token"] = access_token;
            json["refresh_token"] = refresh_token;
            json["token_type"] = token_type;
            json["expires_in"] = expires_in;
            json["user"] = user.ToJson();
            return json;
        }
    };

    /**
     * @brief 刷新令牌响应 DTO
     *
     * @details
     * 包含新的访问令牌和刷新令牌。
     */
    struct RefreshTokenResponse {
        std::string access_token;
        std::string refresh_token;
        int expires_in;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            json["access_token"] = access_token;
            json["refresh_token"] = refresh_token;
            json["expires_in"] = expires_in;
            return json;
        }
    };

} // namespace disk::auth
