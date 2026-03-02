/**
 * @file AuthService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief High-level auth orchestration for the QML client
 * @version 0.1
 * @date 2026-03-02
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
}

namespace disk::qml::services {
    class TokenStore;
}

namespace disk::qml::services {

/**
 * @brief Authentication service for the QML client.
 * @details Orchestrates the full auth lifecycle:
 *   - Input validation (username, email, password formats)
 *   - Delegating network requests to api::AuthApi
 *   - Mapping transport-level and envelope-level errors to user-friendly messages
 *   - Persisting tokens via TokenStore on successful login/refresh
 *   - Clearing tokens on logout
 */
    class AuthService final {
    public:
        using RegisterCallback = std::function<void(std::optional<models::RegisterResultDto> result, QString errorMessage)>;
        using LoginCallback = std::function<void(std::optional<models::LoginResultDto> result, QString errorMessage)>;
        using RefreshCallback = std::function<void(std::optional<models::RefreshResultDto> result, QString errorMessage)>;
        using LogoutCallback = std::function<void(bool ok, QString errorMessage)>;

        AuthService(api::AuthApi* authApi, TokenStore* tokenStore);

        /**
         * @brief Validates a username string.
         * @details Enforces the regex `^[a-zA-Z0-9_]{4,32}$`:
         *   alphanumeric and underscore, 4–32 characters.
         * @param username The username to validate.
         * @return true if the format is valid.
         */
        auto ValidateUsername(const QString& username) const -> bool;
        /**
         * @brief Validates an email address string.
         * @details Enforces a standard email regex:
         *   `^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$`.
         * @param email The email to validate.
         * @return true if the format is valid.
         */
        auto ValidateEmail(const QString& email) const -> bool;
        /**
         * @brief Validates a password string.
         * @details Enforces `^(?=.*[a-z])(?=.*[A-Z])(?=.*\d)[a-zA-Z\d]{8,64}$`:
         *   at least one lowercase, one uppercase, one digit, 8–64 characters.
         * @param password The password to validate.
         * @return true if the format is valid.
         */
        auto ValidatePassword(const QString& password) const -> bool;

        /**
         * @brief Registers a new user account.
         * @details Validates inputs locally before calling api::AuthApi::Register.
         *   On success the callback receives a populated RegisterResultDto.
         *   Tokens are NOT saved on registration; the caller must log in afterwards.
         * @param username   Display name (validated by ValidateUsername).
         * @param email      Email address (validated by ValidateEmail).
         * @param password   Plaintext password (validated by ValidatePassword).
         * @param ctx        QObject lifetime guard; the callback is not invoked after ctx is destroyed.
         * @param cb         Receives (result, errorMessage). errorMessage is empty on success.
         */
        auto Register(const QString& username, const QString& email, const QString& password, QObject* ctx, RegisterCallback cb) -> void;
        /**
         * @brief Authenticates a user and persists the returned tokens.
         * @details Accepts either a username or an email as `account`.
         *   On success, tokens are saved to TokenStore.
         * @param account    Username or email address.
         * @param password   Plaintext password.
         * @param ctx        QObject lifetime guard.
         * @param cb         Receives (result, errorMessage). errorMessage is empty on success.
         */
        auto Login(const QString& account, const QString& password, QObject* ctx, LoginCallback cb) -> void;
        /**
         * @brief Exchanges a refresh token for a new access/refresh token pair.
         * @details On success, the new tokens are saved to TokenStore, replacing the old ones.
         * @param refreshToken  The current refresh token.
         * @param ctx           QObject lifetime guard.
         * @param cb            Receives (result, errorMessage). errorMessage is empty on success.
         */
        auto Refresh(const QString& refreshToken, QObject* ctx, RefreshCallback cb) -> void;
        /**
         * @brief Revokes the access token on the server and clears local tokens.
         * @details If `accessToken` is empty the local store is cleared immediately without
         *   a network call. Certain token-error codes (expired, revoked, malformed, etc.)
         *   are treated as a successful logout and still cause local tokens to be cleared.
         * @param accessToken  The bearer token to revoke (may be empty).
         * @param ctx          QObject lifetime guard.
         * @param cb           Receives (ok, errorMessage). ok=true when tokens were cleared.
         */
        auto Logout(const QString& accessToken, QObject* ctx, LogoutCallback cb) -> void;

    private:
        auto MapTransportError(const QString& networkError) const -> QString;
        auto MapEnvelopeError(const models::ApiEnvelope& envelope) const -> QString;
        auto IsLocalLogoutSuccessCode(int code) const -> bool;

        api::AuthApi* m_auth_api;
        TokenStore* m_token_store;
    };

} // namespace disk::qml::services
