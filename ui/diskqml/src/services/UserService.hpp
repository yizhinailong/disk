/**
 * @file UserService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief High-level user domain orchestration for the QML client
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <optional>

#include <dtos/UserDtos.hpp>

namespace disk::qml::api {
    class UserApi;
}

namespace disk::qml::services {

    /**
     * @brief User service for the QML client.
     * @details Orchestrates user-related business workflows:
     *   - Fetching user profile data
     *   - Fetching storage usage statistics
     *   - Updating user profile (nickname, avatar)
     *   - Changing password
     *   - Delegating network requests to api::UserApi
     *   - Mapping transport-level and envelope-level errors to user-friendly messages
     */
    class UserService final {
    public:
        using ProfileCallback = std::function<void(std::optional<models::UserProfileDto> result, QString errorMessage)>;
        using StorageCallback = std::function<void(std::optional<models::StorageDto> result, QString errorMessage)>;
        using UpdateProfileCallback = std::function<void(std::optional<models::UpdateProfileResultDto> result, QString errorMessage)>;
        using ChangePasswordCallback = std::function<void(std::optional<models::ChangePasswordResultDto> result, QString errorMessage)>;

        explicit UserService(api::UserApi* userApi);

        /**
         * @brief Fetches the authenticated user's profile.
         * @details Calls GET /api/user/profile.
         * @param ctx  QObject lifetime guard; the callback is not invoked after ctx is destroyed.
         * @param cb   Receives (result, errorMessage). errorMessage is empty on success.
         */
        auto GetProfile(QObject* ctx, ProfileCallback cb) -> void;

        /**
         * @brief Fetches storage usage statistics.
         * @details Calls GET /api/user/storage.
         * @param ctx  QObject lifetime guard.
         * @param cb   Receives (result, errorMessage). errorMessage is empty on success.
         */
        auto GetStorage(QObject* ctx, StorageCallback cb) -> void;

        /**
         * @brief Updates the authenticated user's profile.
         * @details Calls PATCH /api/user/profile.
         *   At least one of nickname or avatar must be non-empty.
         * @param nickname  New nickname (empty string → omit from request).
         * @param avatar    New avatar URL (empty string → omit from request).
         * @param ctx       QObject lifetime guard.
         * @param cb        Receives (result, errorMessage). errorMessage is empty on success.
         */
        auto UpdateProfile(const QString& nickname, const QString& avatar, QObject* ctx, UpdateProfileCallback cb) -> void;

        /**
         * @brief Changes the authenticated user's password.
         * @details Calls PUT /api/user/password.
         *   Validates password format locally before making the API call.
         * @param oldPassword  Current password (for verification).
         * @param newPassword  New password (8-64 chars, must contain uppercase, lowercase, digit).
         * @param ctx          QObject lifetime guard.
         * @param cb           Receives (result, errorMessage). errorMessage is empty on success.
         */
        auto ChangePassword(const QString& oldPassword, const QString& newPassword, QObject* ctx, ChangePasswordCallback cb) -> void;

    private:
        auto MapTransportError(const QString& networkError) const -> QString;
        auto MapEnvelopeError(const models::ApiEnvelope& envelope) const -> QString;
        auto ValidatePassword(const QString& password) const -> bool;

        api::UserApi* m_user_api;
    };

} // namespace disk::qml::services
