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

namespace disk::Auth {

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
        static auto FromRequest(const HttpRequestPtr& req) -> Result<RegisterRequest> {
            auto json_ptr = req->getJsonObject();
            if (!json_ptr) {
                return std::unexpected(ErrorCode::ValidationFailed);
            }

            const auto& json = *json_ptr;

            // 检查必填字段
            if (!json_ptr->isMember("username") ||
                !json_ptr->isMember("email") ||
                !json_ptr->isMember("password")) {
                return std::unexpected(ErrorCode::ValidationFailed);
            }

            RegisterRequest request;
            request.username = json["username"].asString();
            request.email = json["email"].asString();
            request.password = json["password"].asString();

            // 验证字段
            if (auto result = request.Validate(); !result) {
                return std::unexpected(result.error());
            }

            return request;
        }

    private:
        /// 验证字段合法性
        [[nodiscard]]
        auto Validate() const -> VoidResult {
            // 用户名验证: 4-32字符，字母数字下划线
            if (username.length() < 4 || username.length() > 32) {
                return std::unexpected(ErrorCode::InvalidFormat);
            }
            static const std::regex usernameRegex("^[a-zA-Z0-9_]+$");
            if (!std::regex_match(username, usernameRegex)) {
                return std::unexpected(ErrorCode::InvalidFormat);
            }

            // 邮箱验证
            static const std::regex emailRegex(R"(^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$)");
            if (!std::regex_match(email, emailRegex)) {
                return std::unexpected(ErrorCode::InvalidFormat);
            }

            // 密码验证: 8-64字符，需含大小写字母和数字
            if (password.length() < 8 || password.length() > 64) {
                return std::unexpected(ErrorCode::InvalidFormat);
            }
            bool hasUpper = false;
            bool hasLower = false;
            bool hasDigit = false;
            for (char c : password) {
                if (std::isupper(c) != 0) {
                    hasUpper = true;
                }
                if (std::islower(c) != 0) {
                    hasLower = true;
                }
                if (std::isdigit(c) != 0) {
                    hasDigit = true;
                }
            }
            if (!hasUpper || !hasLower || !hasDigit) {
                return std::unexpected(ErrorCode::InvalidFormat);
            }

            return {};
        }
    };

} // namespace disk::Auth
