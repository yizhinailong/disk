#include "RegisterViewModel.hpp"

#include <services/AuthService.hpp>

namespace disk::qml::viewmodels {

    RegisterViewModel::RegisterViewModel(services::AuthService* authService, QObject* parent)
        : QObject(parent), m_auth_service(authService) {
    }

    auto RegisterViewModel::Username() const -> const QString& {
        return m_username;
    }

    auto RegisterViewModel::Email() const -> const QString& {
        return m_email;
    }

    auto RegisterViewModel::Password() const -> const QString& {
        return m_password;
    }

    auto RegisterViewModel::ConfirmPassword() const -> const QString& {
        return m_confirm_password;
    }

    auto RegisterViewModel::Loading() const -> bool {
        return m_loading;
    }

    auto RegisterViewModel::ErrorMessage() const -> const QString& {
        return m_error_message;
    }

    auto RegisterViewModel::CanSubmit() const -> bool {
        return m_can_submit;
    }

    auto RegisterViewModel::UsernameError() const -> const QString& {
        return m_username_error;
    }

    auto RegisterViewModel::EmailError() const -> const QString& {
        return m_email_error;
    }

    auto RegisterViewModel::PasswordError() const -> const QString& {
        return m_password_error;
    }

    auto RegisterViewModel::ConfirmPasswordError() const -> const QString& {
        return m_confirm_password_error;
    }

    auto RegisterViewModel::SetUsername(const QString& username) -> void {
        if (m_username == username) {
            return;
        }
        m_username = username;
        emit usernameChanged();
        ValidateUsername();
        UpdateCanSubmit();
    }

    auto RegisterViewModel::SetEmail(const QString& email) -> void {
        if (m_email == email) {
            return;
        }
        m_email = email;
        emit emailChanged();
        ValidateEmail();
        UpdateCanSubmit();
    }

    auto RegisterViewModel::SetPassword(const QString& password) -> void {
        if (m_password == password) {
            return;
        }
        m_password = password;
        emit passwordChanged();
        ValidatePassword();
        ValidateConfirmPassword();
        UpdateCanSubmit();
    }

    auto RegisterViewModel::SetConfirmPassword(const QString& confirmPassword) -> void {
        if (m_confirm_password == confirmPassword) {
            return;
        }
        m_confirm_password = confirmPassword;
        emit confirmPasswordChanged();
        ValidateConfirmPassword();
        UpdateCanSubmit();
    }

    void RegisterViewModel::submit() {
        if (!m_can_submit || m_loading) {
            return;
        }

        SetErrorMessage({});
        SetLoading(true);

        // Context object pattern: create a child QObject whose lifetime is tied to `this`.
        // If `this` (the ViewModel) is destroyed, the context is destroyed too,
        // and Qt's QRestAccessManager will NOT invoke the callback — preventing crashes.
        auto* ctx = new QObject(this);

        m_auth_service->Register(
            m_username,
            m_email,
            m_password,
            ctx,
            [this, ctx](std::optional<models::RegisterResultDto> result, QString errorMessage) {
                // Clean up context object
                ctx->deleteLater();

                SetLoading(false);

                if (result) {
                    emit registerSucceeded(result->user.username, result->user.email);
                    return;
                }

                SetErrorMessage(errorMessage);
            }
        );
    }

    void RegisterViewModel::clearError() {
        SetErrorMessage({});
        SetUsernameError({});
        SetEmailError({});
        SetPasswordError({});
        SetConfirmPasswordError({});
    }

    auto RegisterViewModel::ValidateUsername() -> void {
        if (m_username.isEmpty()) {
            SetUsernameError({});
            return;
        }
        if (!m_auth_service->ValidateUsername(m_username)) {
            SetUsernameError(QStringLiteral("需为4-32个字符，仅支持字母、数字、下划线"));
        } else {
            SetUsernameError({});
        }
    }

    auto RegisterViewModel::ValidateEmail() -> void {
        if (m_email.isEmpty()) {
            SetEmailError({});
            return;
        }
        if (!m_auth_service->ValidateEmail(m_email)) {
            SetEmailError(QStringLiteral("请输入有效的邮箱格式"));
        } else {
            SetEmailError({});
        }
    }

    auto RegisterViewModel::ValidatePassword() -> void {
        if (m_password.isEmpty()) {
            SetPasswordError({});
            return;
        }
        if (!m_auth_service->ValidatePassword(m_password)) {
            SetPasswordError(QStringLiteral("8-64个字符，必须同时包含大小写字母和数字，仅支持字母和数字"));
        } else {
            SetPasswordError({});
        }
    }

    auto RegisterViewModel::ValidateConfirmPassword() -> void {
        if (m_confirm_password.isEmpty()) {
            SetConfirmPasswordError({});
            return;
        }
        if (m_confirm_password != m_password) {
            SetConfirmPasswordError(QStringLiteral("两次输入的密码不一致"));
        } else {
            SetConfirmPasswordError({});
        }
    }

    auto RegisterViewModel::UpdateCanSubmit() -> void {
        const bool canSubmit =
            !m_loading &&
            !m_username.isEmpty() &&
            !m_email.isEmpty() &&
            !m_password.isEmpty() &&
            !m_confirm_password.isEmpty() &&
            m_username_error.isEmpty() &&
            m_email_error.isEmpty() &&
            m_password_error.isEmpty() &&
            m_confirm_password_error.isEmpty();

        if (m_can_submit == canSubmit) {
            return;
        }
        m_can_submit = canSubmit;
        emit canSubmitChanged();
    }

    auto RegisterViewModel::SetLoading(bool loading) -> void {
        if (m_loading == loading) {
            return;
        }
        m_loading = loading;
        emit loadingChanged();
        UpdateCanSubmit();
    }

    auto RegisterViewModel::SetErrorMessage(const QString& message) -> void {
        if (m_error_message == message) {
            return;
        }
        m_error_message = message;
        emit errorMessageChanged();
    }

    auto RegisterViewModel::SetUsernameError(const QString& error) -> void {
        if (m_username_error == error) {
            return;
        }
        m_username_error = error;
        emit usernameErrorChanged();
    }

    auto RegisterViewModel::SetEmailError(const QString& error) -> void {
        if (m_email_error == error) {
            return;
        }
        m_email_error = error;
        emit emailErrorChanged();
    }

    auto RegisterViewModel::SetPasswordError(const QString& error) -> void {
        if (m_password_error == error) {
            return;
        }
        m_password_error = error;
        emit passwordErrorChanged();
    }

    auto RegisterViewModel::SetConfirmPasswordError(const QString& error) -> void {
        if (m_confirm_password_error == error) {
            return;
        }
        m_confirm_password_error = error;
        emit confirmPasswordErrorChanged();
    }

} // namespace disk::qml::viewmodels
