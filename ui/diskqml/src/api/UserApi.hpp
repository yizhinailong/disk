/**
 * @file UserApi.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief User API endpoints for profile, storage, and password management
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <QObject>
#include <QString>

#include <dtos/ApiEnvelope.hpp>

namespace disk::qml::api {

    using UserApiCallback = models::ApiCallback;

    class ApiClient;

    /**
     * @brief API wrapper for user-related endpoints (all require JWT auth).
     *
     * @details
     * All methods use the shared bearer token configured on ApiClient.
     * The caller must ensure the token is set (via ApiClient::SetBearerToken)
     * before invoking these methods.
     *
     * Wrapped endpoints:
     * - GET   /api/user/profile  → UserProfileResponse
     * - GET   /api/user/storage  → StorageResponse
     * - PATCH /api/user/profile  → UpdateProfileRequest
     * - PUT   /api/user/password → ChangePasswordRequest
     */
    class UserApi {
    public:
        /**
         * @brief Construct a UserApi bound to the given API client.
         *
         * @param client  Pointer to the shared ApiClient. The caller is responsible for
         *                ensuring @p client outlives this UserApi instance.
         */
        explicit UserApi(ApiClient* client);

        /**
         * @brief GET /api/user/profile — fetch the authenticated user's profile.
         *
         * @details
         * Response data shape: { "user": { id, username, email, nickname, avatar,
         *                        storage_used, storage_quota, file_count, folder_count,
         *                        created_at, updated_at } }
         *
         * @param ctx  Context QObject; callback suppressed after destruction.
         * @param cb   Invoked with the server envelope on completion.
         */
        virtual auto GetProfile(QObject* ctx, UserApiCallback cb) -> void;

        /**
         * @brief GET /api/user/storage — fetch storage usage statistics.
         *
         * @details
         * Response data shape: { "storage": { used, quota, percentage,
         *                        file_count, folder_count, categories: [...] } }
         *
         * @param ctx  Context QObject; callback suppressed after destruction.
         * @param cb   Invoked with the server envelope on completion.
         */
        virtual auto GetStorage(QObject* ctx, UserApiCallback cb) -> void;

        /**
         * @brief PATCH /api/user/profile — update the authenticated user's profile.
         *
         * @details
         * At least one of @p nickname or @p avatar must be non-empty.
         * Empty strings are omitted from the request body.
         *
         * @param nickname  New display name (empty string → omitted).
         * @param avatar    New avatar URL (empty string → omitted).
         * @param ctx       Context QObject; callback suppressed after destruction.
         * @param cb        Invoked with the server envelope on completion.
         */
        virtual auto UpdateProfile(
            const QString& nickname,
            const QString& avatar,
            QObject* ctx,
            UserApiCallback cb
        ) -> void;

        /**
         * @brief PUT /api/user/password — change the authenticated user's password.
         *
         * @param oldPassword  Current password for verification.
         * @param newPassword  New password (8-64 chars, upper + lower + digit).
         * @param ctx          Context QObject; callback suppressed after destruction.
         * @param cb           Invoked with the server envelope on completion.
         */
        virtual auto ChangePassword(
            const QString& oldPassword,
            const QString& newPassword,
            QObject* ctx,
            UserApiCallback cb
        ) -> void;

        virtual ~UserApi() = default;

    private:
        ApiClient* m_client;
    };

} // namespace disk::qml::api
