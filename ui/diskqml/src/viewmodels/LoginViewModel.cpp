#include "LoginViewModel.hpp"

#include <QJSEngine>
#include <QQmlEngine>

#include <services/AuthService.hpp>
namespace disk::qml::viewmodels {

    LoginViewModel::LoginViewModel(services::AuthService* authService, QObject* parent)
        : QObject(parent), m_auth_service(authService) {
    }

    auto LoginViewModel::Account() const -> const QString& {
        return m_account;
    }

    auto LoginViewModel::Password() const -> const QString& {
        return m_password;
    }

    auto LoginViewModel::Loading() const -> bool {
        return m_loading;
    }

    auto LoginViewModel::ErrorMessage() const -> const QString& {
        return m_error_message;
    }

    auto LoginViewModel::CanSubmit() const -> bool {
        return m_can_submit;
    }

    auto LoginViewModel::SetAccount(const QString& account) -> void {
        if (m_account == account) {
            return;
        }
        m_account = account;
        emit accountChanged();
        UpdateCanSubmit();
    }

    auto LoginViewModel::SetPassword(const QString& password) -> void {
        if (m_password == password) {
            return;
        }
        m_password = password;
        emit passwordChanged();
        UpdateCanSubmit();
    }

    void LoginViewModel::submit() {
        if (!m_can_submit || m_loading) {
            return;
        }

        SetLoading(true);
        SetErrorMessage(QString{});

        // Context object prevents callback invocation if LoginViewModel is destroyed
        auto* ctx = new QObject(this);

        m_auth_service->Login(
            m_account.trimmed(),
            m_password,
            ctx,
            [this, ctx](std::optional<models::LoginResultDto> result, QString errorMessage) {
                // Clean up context object
                ctx->deleteLater();

                SetLoading(false);

                if (!result) {
                    SetErrorMessage(errorMessage);
                    return;
                }

                emit loginSucceeded(result->user.username);
            }
        );
    }

    void LoginViewModel::clearError() {
        SetErrorMessage(QString{});
    }

    auto LoginViewModel::UpdateCanSubmit() -> void {
        const bool canSubmit = !m_account.trimmed().isEmpty() && !m_password.isEmpty() && !m_loading;
        if (m_can_submit == canSubmit) {
            return;
        }
        m_can_submit = canSubmit;
        emit canSubmitChanged();
    }

    auto LoginViewModel::SetLoading(bool loading) -> void {
        if (m_loading == loading) {
            return;
        }
        m_loading = loading;
        emit loadingChanged();
        UpdateCanSubmit();
    }

    auto LoginViewModel::SetErrorMessage(const QString& message) -> void {
        if (m_error_message == message) {
            return;
        }
        m_error_message = message;
        emit errorMessageChanged();
    }

    auto LoginViewModel::SetInstance(LoginViewModel* instance) -> void {
        s_instance = instance;
    }

    auto LoginViewModel::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> LoginViewModel* {
        // The instance has to exist before it is used. We cannot replace it.
        Q_ASSERT(s_instance != nullptr);

        // The engine has to have the same thread affinity as the singleton.
        Q_ASSERT(qmlEngine->thread() == s_instance->thread());

        // There can only be one engine accessing the singleton.
        if (s_engine) {
            Q_ASSERT(jsEngine == s_engine);
        } else {
            s_engine = jsEngine;
        }

        // Explicitly specify C++ ownership so that the engine doesn't delete the instance.
        QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
        return s_instance;
    }

} // namespace disk::qml::viewmodels
