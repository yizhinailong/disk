/**
 * @file AuthService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QML 客户端高级认证编排服务
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <optional>

#include <dtos/AuthDtos.hpp>

namespace disk::qml::api {
    class AuthApi;
    class ApiClient;
} // namespace disk::qml::api

namespace disk::qml::services {
    class TokenStore;
}

namespace disk::qml::services {

    /**
     * @brief QML 客户端认证服务。
     * @details 编排完整的认证生命周期：
     *   - 输入验证（用户名、邮箱、密码格式）
     *   - 委托 api::AuthApi 执行网络请求
     *   - 将传输层和响应封装层错误映射为用户友好消息
     *   - 登录/刷新成功后通过 TokenStore 持久化令牌
     *   - 登出时清除令牌
     */
    class AuthService final {
    public:
        using RegisterCallback = std::function<void(std::optional<models::RegisterResultDto> result, QString errorMessage)>;
        using LoginCallback = std::function<void(std::optional<models::LoginResultDto> result, QString errorMessage)>;
        using RefreshCallback = std::function<void(std::optional<models::RefreshResultDto> result, QString errorMessage)>;
        using LogoutCallback = std::function<void(bool ok, QString errorMessage)>;

        AuthService(api::AuthApi* authApi, TokenStore* tokenStore, api::ApiClient* apiClient);

        /**
         * @brief 验证用户名字符串。
         * @details 强制正则 `^[a-zA-Z0-9_]{4,32}$`：
         *   字母数字和下划线，4–32 个字符。
         * @param username 要验证的用户名。
         * @return 格式有效返回 true。
         */
        auto ValidateUsername(const QString& username) const -> bool;
        /**
         * @brief 验证邮箱地址字符串。
         * @details 强制标准邮箱正则：
         *   `^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$`。
         * @param email 要验证的邮箱。
         * @return 格式有效返回 true。
         */
        auto ValidateEmail(const QString& email) const -> bool;
        /**
         * @brief 验证密码字符串。
         * @details 强制 `^(?=.*[a-z])(?=.*[A-Z])(?=.*\d)[a-zA-Z\d]{8,64}$`：
         *   至少一个小写字母、一个大写字母、一个数字，8–64 个字符。
         * @param password 要验证的密码。
         * @return 格式有效返回 true。
         */
        auto ValidatePassword(const QString& password) const -> bool;

        /**
         * @brief 注册新用户账号。
         * @details 在调用 api::AuthApi::Register 前本地验证输入。
         *   成功时回调接收填充好的 RegisterResultDto。
         *   注册时不保存令牌；调用者必须随后登录。
         * @param username   显示名称（由 ValidateUsername 验证）。
         * @param email      邮箱地址（由 ValidateEmail 验证）。
         * @param password   明文密码（由 ValidatePassword 验证）。
         * @param ctx        QObject 生命周期守护；ctx 销毁后不调用回调。
         * @param cb         接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto Register(const QString& username, const QString& email, const QString& password, QObject* ctx, RegisterCallback cb) -> void;
        /**
         * @brief 认证用户并持久化返回的令牌。
         * @details 接受用户名或邮箱作为 `account`。
         *   成功时令牌保存到 TokenStore。
         * @param account    用户名或邮箱地址。
         * @param password   明文密码。
         * @param ctx        QObject 生命周期守护。
         * @param cb         接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto Login(const QString& account, const QString& password, QObject* ctx, LoginCallback cb) -> void;
        /**
         * @brief 用刷新令牌换取新的访问/刷新令牌对。
         * @details 成功时新令牌保存到 TokenStore，替换旧令牌。
         * @param refreshToken  当前刷新令牌。
         * @param ctx           QObject 生命周期守护。
         * @param cb            接收 (result, errorMessage)。成功时 errorMessage 为空。
         */
        auto Refresh(const QString& refreshToken, QObject* ctx, RefreshCallback cb) -> void;
        /**
         * @brief 在服务器上撤销访问令牌并清除本地令牌。
         * @details 如果 `accessToken` 为空，立即清除本地存储而不发起网络请求。
         *   某些令牌错误码（过期、已撤销、格式错误等）视为成功登出，
         *   仍会清除本地令牌。
         * @param accessToken  要撤销的 Bearer 令牌（可为空）。
         * @param ctx          QObject 生命周期守护。
         * @param cb           接收 (ok, errorMessage)。令牌清除时 ok=true。
         */
        auto Logout(const QString& accessToken, QObject* ctx, LogoutCallback cb) -> void;

    private:
        auto MapTransportError(const QString& networkError) const -> QString;
        auto MapEnvelopeError(const models::ApiEnvelope& envelope) const -> QString;
        auto IsLocalLogoutSuccessCode(int code) const -> bool;

        api::AuthApi* m_auth_api;
        TokenStore* m_token_store;
        api::ApiClient* m_api_client;
    };

} // namespace disk::qml::services
