/**
 * @file AuthDto.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 认证模块数据传输对象（Data Transfer Objects）
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

#include "utils/DtoBase.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::auth {

    /// ==================== Request DTOs ====================

    /**
     * @brief 用户注册请求 DTO
     *
     * @details
     * 验证规则：
     * - username: 4-32字符，字母数字下划线
     * - email: 有效邮箱格式
     * - password: 8-64字符，仅含字母和数字，且需同时包含大小写字母和数字
     */
    struct RegisterRequest : DtoBase<RegisterRequest> {
        std::string username;
        std::string email;
        std::string password;

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(
            const drogon::HttpRequestPtr& req,
            disk::utils::LogContext log_context = {}
        ) -> Result<RegisterRequest> {
            Logger::Debug(log_context) << "Start parsing register request parameters";

            auto json_result = RequireJsonBody(req);
            if (!json_result) {
                return std::unexpected(json_result.error());
            }
            const auto& json = *json_result.value();

            auto username_result = RequireString(json, "username");
            if (!username_result) {
                return std::unexpected(username_result.error());
            }

            auto email_result = RequireString(json, "email");
            if (!email_result) {
                return std::unexpected(email_result.error());
            }

            auto password_result = RequireString(json, "password");
            if (!password_result) {
                return std::unexpected(password_result.error());
            }

            RegisterRequest request;
            request.username = std::move(*username_result);
            request.email = std::move(*email_result);
            request.password = std::move(*password_result);

            Logger::Debug(log_context)
                << "Parsed register request: " << request.username << " <" << request.email << ">";

            if (!request.ValidateUsername()) {
                Logger::Warn(log_context) << "Username format error: " << request.username;
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Username format error")
                );
            }
            if (!request.ValidateEmail()) {
                Logger::Warn(log_context) << "Email format error: " << request.email;
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Email format error")
                );
            }
            if (!request.ValidatePassword()) {
                Logger::Warn(log_context) << "Password format error: " << request.username;
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Password format error")
                );
            }
            Logger::Debug(log_context) << "Request parameters validated";

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
            static const std::regex email_regex(
                R"(^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$)"
            );
            return std::regex_match(email, email_regex);
        }

        /// 验证密码
        [[nodiscard]]
        auto ValidatePassword() const -> bool {
            if (password.length() < 8 || password.length() > 64) {
                return false;
            }
            static const std::regex password_regex(
                "^(?=.*[a-z])(?=.*[A-Z])(?=.*\\d)[a-zA-Z\\d]{8,64}$"
            );
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
    struct LoginRequest : DtoBase<LoginRequest> {
        std::string account;
        std::string password;

        [[nodiscard]]
        static auto FromRequest(
            const drogon::HttpRequestPtr& req,
            disk::utils::LogContext log_context = {}
        ) -> Result<LoginRequest> {
            Logger::Debug(log_context) << "Start parsing login request parameters";

            auto json_result = RequireJsonBody(req);
            if (!json_result) {
                return std::unexpected(json_result.error());
            }
            const auto& json = *json_result.value();

            auto account_result = RequireString(json, "account");
            if (!account_result) {
                return std::unexpected(account_result.error());
            }

            auto password_result = RequireString(json, "password");
            if (!password_result) {
                return std::unexpected(password_result.error());
            }

            LoginRequest request;
            request.account = std::move(*account_result);
            request.password = std::move(*password_result);

            if (request.account.empty()) {
                Logger::Warn(log_context) << "Account cannot be empty";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Account cannot be empty")
                );
            }
            if (request.password.empty()) {
                Logger::Warn(log_context) << "Password cannot be empty";
                return std::unexpected(
                    ErrorInfo(ErrorCode::ValidationFailed, "Password cannot be empty")
                );
            }

            Logger::Debug(log_context) << "Parsed login request: " << request.account;

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
    struct RefreshTokenRequest : DtoBase<RefreshTokenRequest> {
        std::string refresh_token;

        [[nodiscard]]
        static auto FromRequest(
            const drogon::HttpRequestPtr& req,
            disk::utils::LogContext log_context = {}
        ) -> Result<RefreshTokenRequest> {
            Logger::Debug(log_context) << "Start parsing refresh token request parameters";

            auto json_result = RequireJsonBody(req);
            if (!json_result) {
                return std::unexpected(json_result.error());
            }
            const auto& json = *json_result.value();

            auto token_result = RequireString(json, "refresh_token");
            if (!token_result) {
                return std::unexpected(token_result.error());
            }

            RefreshTokenRequest request;
            request.refresh_token = std::move(*token_result);

            Logger::Debug(log_context) << "Parsed refresh token request";

            return request;
        }
    };

    /// ==================== Response DTOs ====================

    /**
     * @brief 用户注册响应 DTO
     *
     * @details
     * 包含用户的基本信息和存储配额。
     */
    struct RegisterResponse : DtoBase<RegisterResponse> {
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
            SetField(json, "id", id);
            SetField(json, "username", username);
            SetField(json, "email", email);
            SetField(json, "nickname", nickname);
            SetField(json, "storage_quota", storage_quota);
            SetField(json, "storage_used", storage_used);
            SetField(json, "created_at", created_at);
            return json;
        }
    };

    /**
     * @brief 用户登录响应 DTO
     *
     * @details
     * 包含访问令牌、刷新令牌和用户信息。
     */
    struct LoginResponse : DtoBase<LoginResponse> {
        std::string access_token;
        std::string refresh_token;
        std::string token_type;
        int expires_in;
        RegisterResponse user;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "access_token", access_token);
            SetField(json, "refresh_token", refresh_token);
            SetField(json, "token_type", token_type);
            SetField(json, "expires_in", expires_in);
            SetField(json, "user", user);
            return json;
        }
    };

    /**
     * @brief 刷新令牌响应 DTO
     *
     * @details
     * 包含新的访问令牌和刷新令牌。
     */
    struct RefreshTokenResponse : DtoBase<RefreshTokenResponse> {
        std::string access_token;
        std::string refresh_token;
        int expires_in;

        /// 转换为 JSON
        [[nodiscard]]
        auto ToJson() const -> Json::Value {
            Json::Value json;
            SetField(json, "access_token", access_token);
            SetField(json, "refresh_token", refresh_token);
            SetField(json, "expires_in", expires_in);
            return json;
        }
    };

} // namespace disk::auth
