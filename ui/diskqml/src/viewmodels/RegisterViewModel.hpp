#pragma once

#include <QObject>
#include <QString>

namespace disk::qml::services {
    class AuthService;
}

namespace disk::qml::viewmodels {

    class RegisterViewModel : public QObject {
        Q_OBJECT

        Q_PROPERTY(QString username READ Username WRITE SetUsername NOTIFY usernameChanged)
        Q_PROPERTY(QString email READ Email WRITE SetEmail NOTIFY emailChanged)
        Q_PROPERTY(QString password READ Password WRITE SetPassword NOTIFY passwordChanged)
        Q_PROPERTY(QString confirmPassword READ ConfirmPassword WRITE SetConfirmPassword NOTIFY confirmPasswordChanged)
        Q_PROPERTY(bool loading READ Loading NOTIFY loadingChanged)
        Q_PROPERTY(QString errorMessage READ ErrorMessage NOTIFY errorMessageChanged)
        Q_PROPERTY(bool canSubmit READ CanSubmit NOTIFY canSubmitChanged)
        Q_PROPERTY(QString usernameError READ UsernameError NOTIFY usernameErrorChanged)
        Q_PROPERTY(QString emailError READ EmailError NOTIFY emailErrorChanged)
        Q_PROPERTY(QString passwordError READ PasswordError NOTIFY passwordErrorChanged)
        Q_PROPERTY(QString confirmPasswordError READ ConfirmPasswordError NOTIFY confirmPasswordErrorChanged)

    public:
        explicit RegisterViewModel(services::AuthService* authService, QObject* parent = nullptr);

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

        Q_INVOKABLE void submit();
        Q_INVOKABLE void clearError();

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
    };

} // namespace disk::qml::viewmodels
