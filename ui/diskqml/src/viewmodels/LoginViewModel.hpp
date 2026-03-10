/**
 * @file LoginViewModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QML singleton ViewModel for login form state
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
}

namespace disk::qml::viewmodels {

    /**
     * @brief QML singleton ViewModel for the login form.
     *
     * @details
     * Exposes login form state (account, password, loading, error) to QML via
     * Q_PROPERTY bindings. Business logic and API calls are handled entirely in C++;
     * QML only drives the UI.
     *
     * Singleton lifecycle: an application-owned instance must be created and
     * registered with SetInstance() before the QML engine calls create().
     */
    class LoginViewModel : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_SINGLETON

        // ==================== Properties ====================

        /// User account input for login (username or email).
        Q_PROPERTY(QString account READ Account WRITE SetAccount NOTIFY accountChanged)
        /// User password input for login.
        Q_PROPERTY(QString password READ Password WRITE SetPassword NOTIFY passwordChanged)
        /// True while the login API call is in flight.
        Q_PROPERTY(bool loading READ Loading NOTIFY loadingChanged)
        /// Non-empty when login failed; cleared by clearError() or a new submit().
        Q_PROPERTY(QString errorMessage READ ErrorMessage NOTIFY errorMessageChanged)
        /// True when account and password are non-empty and no request is in flight.
        Q_PROPERTY(bool canSubmit READ CanSubmit NOTIFY canSubmitChanged)

    public:
        explicit LoginViewModel(services::AuthService* authService, QObject* parent = nullptr);

        // ==================== Public API ====================

        /**
         * @brief Register the pre-created instance for use by the QML engine.
         *
         * @details
         * Must be called before the QML engine requests the singleton via create().
         * Ownership of @p instance remains with the caller (C++ side).
         */
        static auto SetInstance(LoginViewModel* instance) -> void;

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
        static auto create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> LoginViewModel*;

        [[nodiscard]] auto Account() const -> const QString&;
        [[nodiscard]] auto Password() const -> const QString&;
        [[nodiscard]] auto Loading() const -> bool;
        [[nodiscard]] auto ErrorMessage() const -> const QString&;
        [[nodiscard]] auto CanSubmit() const -> bool;

        auto SetAccount(const QString& account) -> void;
        auto SetPassword(const QString& password) -> void;

        /**
         * @brief Submit the login form.
         *
         * @details
         * No-op if canSubmit is false or a request is already in flight.
         * Sets loading=true and clears any previous error, then calls
         * AuthService::Login(). On success, emits loginSucceeded(username, storageUsed, storageQuota).
         * On failure, sets errorMessage and clears loading.
         *
         * A child context QObject is used so that the callback is automatically
         * discarded if this ViewModel is destroyed before the response arrives.
         */
        Q_INVOKABLE void submit();

        /**
         * @brief Clear the current error message.
         *
         * @details
         * Resets errorMessage to an empty string. Useful when the user edits a
         * field after a failed attempt and the UI wants to hide the error banner.
         */
        Q_INVOKABLE void clearError();

        // ==================== Signals ====================

    signals:
        void accountChanged();
        void passwordChanged();
        void loadingChanged();
        void errorMessageChanged();
        void canSubmitChanged();
        void loginSucceeded(const QString& username, quint64 storageUsed, quint64 storageQuota);

    private:
        // ==================== Private Helpers ====================

        auto UpdateCanSubmit() -> void;
        auto SetLoading(bool loading) -> void;
        auto SetErrorMessage(const QString& message) -> void;

        // ==================== State ====================

        services::AuthService* m_auth_service;
        QString m_account;
        QString m_password;
        bool m_loading{ false };
        QString m_error_message;
        bool m_can_submit{ false };

        inline static LoginViewModel* s_instance = nullptr;
        inline static QJSEngine* s_engine = nullptr;
    };

} // namespace disk::qml::viewmodels
