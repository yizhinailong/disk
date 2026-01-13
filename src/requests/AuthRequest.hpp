/**
 * @file AuthRequest.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 认证相关请求结构体定义
 * @version 0.1
 * @date 2026-01-14
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include "utils/ErrorCode.hpp"

namespace disk::auth {

    /**
     * @brief 用户注册请求
     *
     * 验证规则：
     * - username: 4-32字符，字母数字下划线
     * - email: 有效邮箱格式
     * - password: 8-64字符，需含大小写字母和数字
     */
    struct RegisterRequest {
        std::string username;
        std::string email;
        std::string password;

        /// 从 HTTP 请求解析并验证，返回 Result
        [[nodiscard]]
        static auto FromRequest(const drogon::HttpRequestPtr& req) -> Result<RegisterRequest> {
            auto json_ptr = req->getJsonObject();
            if (!json_ptr) {
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "请求体不是有效的 JSON"));
            }

            const auto& json = *json_ptr;

            // 检查必填字段
            if (!json.isMember("username")) {
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: username"));
            }
            if (!json.isMember("email")) {
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: email"));
            }
            if (!json.isMember("password")) {
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "缺少必需参数: password"));
            }

            // 检查字段类型
            if (!json["username"].isString()) {
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'username' 类型错误: 期望字符串"));
            }
            if (!json["email"].isString()) {
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'email' 类型错误: 期望字符串"));
            }
            if (!json["password"].isString()) {
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "参数 'password' 类型错误: 期望字符串"));
            }

            RegisterRequest request;
            request.username = json["username"].asString();
            request.email = json["email"].asString();
            request.password = json["password"].asString();

            if (!request.ValidateUsername()) {
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "用户名格式错误"));
            }
            if (!request.ValidateEmail()) {
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "邮箱格式错误"));
            }
            if (!request.ValidatePassword()) {
                return std::unexpected(ErrorInfo(ErrorCode::ValidationFailed, "密码格式错误"));
            }

            return request;
        }

    private:
        /// 验证用户名
        [[nodiscard]]
        auto ValidateUsername() const -> bool {
            // 验证用户名
            static const std::regex usernameRegex("^[a-zA-Z0-9_]+$");
            return std::regex_match(username, usernameRegex);
        }

        /// 验证邮箱
        [[nodiscard]]
        auto ValidateEmail() const -> bool {
            static const std::regex emailRegex(R"(^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$)");
            return std::regex_match(email, emailRegex);
        }

        /// 验证密码
        [[nodiscard]]
        auto ValidatePassword() const -> bool {
            // 验证密码
            static const std::regex passwordRegex("^(?=.*[a-z])(?=.*[A-Z])(?=.*\\d)[a-zA-Z\\d]{8,64}$");
            return std::regex_match(password, passwordRegex);
        }
    };

} // namespace disk::auth
