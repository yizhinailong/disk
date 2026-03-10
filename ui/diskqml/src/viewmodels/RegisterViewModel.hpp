/**
 * @file RegisterViewModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QML singleton ViewModel for register form state
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
     * @brief QML singleton ViewModel for the registration form.
     *
     * @details
     * Exposes registration form state (username, email, password, confirmPassword,
     * loading, errorMessage, per-field validation errors) to QML via Q_PROPERTY
     * bindings. Inline field validation runs as the user types; a final submit()
     * call triggers the API request. All business logic stays in C++.
     *
     * Singleton boundary audit (Task 7): Page-scoped (registration form state).
     * Kept as QML_SINGLETON for now to preserve typed registration and current
     * imports; planned migration target is explicit page-level instantiation.
     *
     * Singleton lifecycle: an application-owned instance must be created and
     * registered with SetInstance() before the QML engine calls create().
     */
    class RegisterViewModel : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_SINGLETON

        // ==================== Properties ====================

        /// Desired username for the new account (4-32 chars, alphanumeric + underscore).
        Q_PROPERTY(QString username READ Username WRITE SetUsername NOTIFY usernameChanged)
        /// Email address for the new account.
        Q_PROPERTY(QString email READ Email WRITE SetEmail NOTIFY emailChanged)
        /// Password for the new account (8-64 chars, mixed case + digits).
        Q_PROPERTY(QString password READ Password WRITE SetPassword NOTIFY passwordChanged)
        /// Confirmation password — must match password before submit is enabled.
        Q_PROPERTY(QString confirmPassword READ ConfirmPassword WRITE SetConfirmPassword NOTIFY confirmPasswordChanged)
        /// True while the registration API call is in flight.
        Q_PROPERTY(bool loading READ Loading NOTIFY loadingChanged)
        /// Non-empty when registration failed at the API level; cleared by clearError().
        Q_PROPERTY(QString errorMessage READ ErrorMessage NOTIFY errorMessageChanged)
        /// True when all fields are valid, non-empty, and no request is in flight.
        Q_PROPERTY(bool canSubmit READ CanSubmit NOTIFY canSubmitChanged)
        /// Inline validation error for the username field; empty when valid or untouched.
        Q_PROPERTY(QString usernameError READ UsernameError NOTIFY usernameErrorChanged)
        /// Inline validation error for the email field; empty when valid or untouched.
        Q_PROPERTY(QString emailError READ EmailError NOTIFY emailErrorChanged)
        /// Inline validation error for the password field; empty when valid or untouched.
        Q_PROPERTY(QString passwordError READ PasswordError NOTIFY passwordErrorChanged)
        /// Inline validation error for the confirm-password field; empty when it matches password.
        Q_PROPERTY(QString confirmPasswordError READ ConfirmPasswordError NOTIFY confirmPasswordErrorChanged)

    public:
        explicit RegisterViewModel(services::AuthService* authService, QObject* parent = nullptr);

        // ==================== Public API ====================

        /**
         * @brief Register the pre-created instance for use by the QML engine.
         *
         * @details
         * Must be called before the QML engine requests the singleton via create().
         * Ownership of @p instance remains with the caller (C++ side).
         */
        static auto SetInstance(RegisterViewModel* instance) -> void;

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
        static auto create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> RegisterViewModel*;

        [[nodiscard]] auto Username() const -> const QString&;
        [[nodiscard]] auto Email() const -> const QString&;
        [[nodiscard]] auto Password() const -> const QString&;
        [[nodiscard]] auto ConfirmPassword() const -> const QString&;
        [[nodiscard]] auto Loading() const -> bool;
        [[nodiscard]] auto ErrorMessage() const -> const QString&;
        [[nodiscard]] auto CanSubmit() const -> bool;
        [[nodiscard]] auto UsernameError() const -> const QString&;
        [[nodiscard]] auto EmailError() const -> const QString&;
        [[nodiscard]] auto PasswordError() const -> const QString&;
        [[nodiscard]] auto ConfirmPasswordError() const -> const QString&;

        auto SetUsername(const QString& username) -> void;
        auto SetEmail(const QString& email) -> void;
        auto SetPassword(const QString& password) -> void;
        auto SetConfirmPassword(const QString& confirmPassword) -> void;

        /**
         * @brief Submit the registration form.
         *
         * @details
         * No-op if canSubmit is false or a request is already in flight.
         * Clears errorMessage, sets loading=true, then calls
         * AuthService::Register(). On success, emits registerSucceeded(username, email).
         * On failure, sets errorMessage and clears loading.
         *
         * A child context QObject is used so that the callback is automatically
         * discarded if this ViewModel is destroyed before the response arrives.
         */
        Q_INVOKABLE void submit();

        /**
         * @brief Clear all error messages (global and per-field).
         *
         * @details
         * Resets errorMessage, usernameError, emailError, passwordError, and
         * confirmPasswordError to empty strings. Useful when the user navigates
         * away and returns to a clean form state.
         */
        Q_INVOKABLE void clearError();

        // ==================== Signals ====================

    signals:
        void usernameChanged();
        void emailChanged();
        void passwordChanged();
        void confirmPasswordChanged();
        void loadingChanged();
        void errorMessageChanged();
        void canSubmitChanged();
        void usernameErrorChanged();
        void emailErrorChanged();
        void passwordErrorChanged();
        void confirmPasswordErrorChanged();
        void registerSucceeded(const QString& username, const QString& email);

    private:
        // ==================== Private Helpers ====================

        auto ValidateUsername() -> void;
        auto ValidateEmail() -> void;
        auto ValidatePassword() -> void;
        auto ValidateConfirmPassword() -> void;
        auto UpdateCanSubmit() -> void;
        auto SetLoading(bool loading) -> void;
        auto SetErrorMessage(const QString& message) -> void;
        auto SetUsernameError(const QString& error) -> void;
        auto SetEmailError(const QString& error) -> void;
        auto SetPasswordError(const QString& error) -> void;
        auto SetConfirmPasswordError(const QString& error) -> void;

        // ==================== State ====================

        services::AuthService* m_auth_service;
        QString m_username;
        QString m_email;
        QString m_password;
        QString m_confirm_password;
        bool m_loading{ false };
        QString m_error_message;
        bool m_can_submit{ false };
        QString m_username_error;
        QString m_email_error;
        QString m_password_error;
        QString m_confirm_password_error;

        inline static RegisterViewModel* s_instance = nullptr;
        inline static QJSEngine* s_engine = nullptr;
    };

} // namespace disk::qml::viewmodels
