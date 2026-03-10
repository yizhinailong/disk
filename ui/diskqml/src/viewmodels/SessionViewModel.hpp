/**
 * @file SessionViewModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QML singleton session state, storage metrics, and logout action
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <QObject>
#include <QString>

#include <QtQml/qjsengine.h>
#include <QtQml/qqmlengine.h>
#include <QtQml/qqmlregistration.h>

namespace disk::qml::services {
    class AuthService;
    class TokenStore;
} // namespace disk::qml::services

namespace disk::qml::utils {
    class ConfigStore;
} // namespace disk::qml::utils

namespace disk::qml::viewmodels {

    class LoginViewModel;

    /**
     * @brief QML singleton that tracks global session state and provides logout.
     *
     * @details
     * Exposes whether the user is logged in, their username, storage quota/usage
     * metrics, and the configured server URL to QML, reacting to login events
     * from LoginViewModel and token state from TokenStore.
     * The logout() action calls the server API and then unconditionally clears
     * local token state regardless of the server response.
     *
     * Singleton lifecycle: an application-owned instance must be created and
     * registered with SetInstance() before the QML engine calls create().
     */
    class SessionViewModel : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_SINGLETON

        // ==================== Properties ====================

        /// True when a valid access token exists in the local token store.
        Q_PROPERTY(bool isLoggedIn READ IsLoggedIn NOTIFY isLoggedInChanged)
        /// Username of the currently logged-in user; empty when not logged in.
        Q_PROPERTY(QString loggedInUserName READ LoggedInUserName NOTIFY loggedInUserNameChanged)

        /// Storage consumed by the current user (bytes); 0 when not logged in.
        Q_PROPERTY(qint64 storageUsed READ StorageUsed NOTIFY storageUsedChanged)
        /// Storage quota allocated to the current user (bytes); 0 when not logged in.
        Q_PROPERTY(qint64 storageQuota READ StorageQuota NOTIFY storageQuotaChanged)

        /// Human-readable formatted storage used (e.g. "1.23 GB").
        Q_PROPERTY(QString storageUsedFormatted READ StorageUsedFormatted NOTIFY storageUsedChanged)
        /// Human-readable formatted storage quota (e.g. "10.00 GB").
        Q_PROPERTY(QString storageQuotaFormatted READ StorageQuotaFormatted NOTIFY storageQuotaChanged)
        /// Storage usage as a percentage (0.0–100.0); 0.0 when quota is 0.
        Q_PROPERTY(double storagePercentage READ StoragePercentage NOTIFY storagePercentageChanged)

        /// Configured server URL (read-only, set once at construction).
        Q_PROPERTY(QString serverUrl READ ServerUrl CONSTANT)

    public:
        explicit SessionViewModel(
            LoginViewModel* loginViewModel,
            services::TokenStore* tokenStore,
            services::AuthService* authService,
            utils::ConfigStore* configStore,
            QObject* parent = nullptr
        );

        // ==================== Public API ====================

        /**
         * @brief Register the pre-created instance for use by the QML engine.
         *
         * @details
         * Must be called before the QML engine requests the singleton via create().
         * Ownership of @p instance remains with the caller (C++ side).
         */
        static auto SetInstance(SessionViewModel* instance) -> void;

        /**
         * @brief QML singleton factory — called once by the QML engine.
         *
         * @details
         * Constraints enforced at runtime:
         * - s_instance must have been set via SetInstance() beforehand.
         * - @p qmlEngine must share thread affinity with the instance.
         * - Only a single QJSEngine may access this singleton; a second engine
         *   triggers a Q_ASSERT failure.
         * - Ownership is set to CppOwnership to prevent the engine from deleting
         *   the instance when the engine is torn down.
         */
        static auto create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> SessionViewModel*;

        [[nodiscard]] auto IsLoggedIn() const -> bool;
        [[nodiscard]] auto LoggedInUserName() const -> const QString&;
        [[nodiscard]] auto StorageUsed() const -> qint64;
        [[nodiscard]] auto StorageQuota() const -> qint64;
        [[nodiscard]] auto StorageUsedFormatted() const -> QString;
        [[nodiscard]] auto StorageQuotaFormatted() const -> QString;
        [[nodiscard]] auto StoragePercentage() const -> double;
        [[nodiscard]] auto ServerUrl() const -> const QString&;

        /**
         * @brief Send a logout request and clear local session state.
         *
         * @details
         * Sends the current access token to the server via AuthService::Logout().
         * Regardless of server response, clears the TokenStore, resets the
         * logged-in username, and re-evaluates isLoggedIn.
         *
         * A child context QObject is used so that the callback is automatically
         * discarded if this ViewModel is destroyed before the response arrives.
         */
        Q_INVOKABLE void logout();

        // ==================== Signals ====================

    signals:
        void isLoggedInChanged();
        void loggedInUserNameChanged();
        void storageUsedChanged();
        void storageQuotaChanged();
        void storagePercentageChanged();

    private:
        // ==================== Private Helpers ====================

        auto SetLoggedInUserName(const QString& name) -> void;
        auto UpdateIsLoggedIn() -> void;
        auto SetStorageUsed(qint64 bytes) -> void;
        auto SetStorageQuota(qint64 bytes) -> void;
        static auto FormatBytes(qint64 bytes) -> QString;

        // ==================== State ====================

        LoginViewModel* m_login_view_model;
        services::TokenStore* m_token_store;
        services::AuthService* m_auth_service;

        bool m_is_logged_in{ false };
        QString m_logged_in_user_name;
        qint64 m_storage_used{ 0 };
        qint64 m_storage_quota{ 0 };
        QString m_server_url;

        inline static SessionViewModel* s_instance = nullptr;
        inline static QJSEngine* s_engine = nullptr;
    };

} // namespace disk::qml::viewmodels
