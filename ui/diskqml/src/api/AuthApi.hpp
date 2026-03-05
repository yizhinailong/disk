/**
 * @file AuthApi.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Auth API endpoints for register/login/refresh/logout
 * @version 0.1
 * @date 2026-03-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <QObject>
#include <QString>

#include <dtos/ApiEnvelope.hpp>
#include <dtos/AuthDtos.hpp>

namespace disk::qml::api {

    using AuthApiCallback = models::ApiCallback;

    class ApiClient;

    class AuthApi {
    public:
        /**
         * @brief Construct an AuthApi bound to the given API client.
         *
         * @param client  Pointer to the shared ApiClient. The caller is responsible for
         *                ensuring @p client outlives this AuthApi instance.
         */
        explicit AuthApi(ApiClient* client);

        /**
         * @brief POST /api/auth/register — create a new user account.
         *
         * @param username  Desired username.
         * @param email     User email address.
         * @param password  Plain-text password (hashed server-side).
         * @param ctx       Context QObject; callback suppressed after destruction.
         * @param cb        Invoked with the server envelope on completion.
         */
        virtual auto Register(
            const QString& username,
            const QString& email,
            const QString& password,
            QObject* ctx,
            AuthApiCallback cb
        ) -> void;

        /**
         * @brief POST /api/auth/login — authenticate and obtain tokens.
         *
         * @param account   Username or email.
         * @param password  Plain-text password.
         * @param ctx       Context QObject; callback suppressed after destruction.
         * @param cb        Invoked with the server envelope on completion.
         */
        virtual auto Login(const QString& account, const QString& password, QObject* ctx, AuthApiCallback cb) -> void;

        /**
         * @brief POST /api/auth/refresh — exchange a refresh token for new tokens.
         *
         * @param refreshToken  Single-use refresh token obtained from login.
         * @param ctx           Context QObject; callback suppressed after destruction.
         * @param cb            Invoked with the server envelope on completion.
         */
        virtual auto Refresh(const QString& refreshToken, QObject* ctx, AuthApiCallback cb) -> void;

        /**
         * @brief POST /api/auth/logout — invalidate the access token.
         *
         * Passes @p accessToken as a Bearer token for this request only, without
         * modifying the shared factory token state.
         *
         * @param accessToken  Current access token to invalidate.
         * @param ctx          Context QObject; callback suppressed after destruction.
         * @param cb           Invoked with the server envelope on completion.
         */
        virtual auto Logout(const QString& accessToken, QObject* ctx, AuthApiCallback cb) -> void;

        virtual ~AuthApi() = default;

    private:
        ApiClient* m_client;
    };

} // namespace disk::qml::api
