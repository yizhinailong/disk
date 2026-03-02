#include "AppContext.hpp"

#include <api/ApiClient.hpp>
#include <api/AuthApi.hpp>
#include <services/AuthService.hpp>
#include <storage/TokenStore.hpp>
#include <utils/ConfigStore.hpp>
#include <viewmodels/LoginViewModel.hpp>
#include <viewmodels/RegisterViewModel.hpp>

namespace disk::qml::app {

    AppContext::AppContext(
        utils::ConfigStore* configStore,
        storage::TokenStore* tokenStore,
        api::ApiClient* apiClient,
        api::AuthApi* authApi,
        services::AuthService* authService,
        viewmodels::LoginViewModel* loginViewModel,
        viewmodels::RegisterViewModel* registerViewModel,
        QObject* parent
    )
        : QObject(parent), m_config_store(configStore), m_token_store(tokenStore), m_api_client(apiClient), m_auth_api(authApi), m_auth_service(authService), m_login_view_model(loginViewModel), m_register_view_model(registerViewModel) {
        // Update isLoggedIn based on existing token state
        m_is_logged_in = m_token_store->HasValidAccessToken();

        // Connect login success → update username and logged-in state
        connect(m_login_view_model, &viewmodels::LoginViewModel::loginSucceeded, this, [this](const QString& username) {
            SetLoggedInUserName(username);
            UpdateIsLoggedIn();
        });

        // Connect register success → (user still needs to login, but cache username)
        connect(m_register_view_model, &viewmodels::RegisterViewModel::registerSucceeded, this, [this](const QString& /*username*/, const QString& /*email*/) {
            // Registration doesn't auto-login; no state change needed here
        });
    }

    auto AppContext::LoginViewModel() const -> QObject* {
        return m_login_view_model;
    }

    auto AppContext::RegisterViewModel() const -> QObject* {
        return m_register_view_model;
    }

    auto AppContext::IsLoggedIn() const -> bool {
        return m_is_logged_in;
    }

    auto AppContext::LoggedInUserName() const -> const QString& {
        return m_logged_in_user_name;
    }

    void AppContext::logout() {
        const QString accessToken = m_token_store->AccessToken();

        // Context object prevents callback invocation if AppContext is destroyed
        auto* ctx = new QObject(this);

        m_auth_service->Logout(
            accessToken,
            ctx,
            [this, ctx](bool /*ok*/, QString /*errorMessage*/) {
                ctx->deleteLater();

                // Clear local state regardless of server response
                m_token_store->Clear();
                SetLoggedInUserName(QString{});
                UpdateIsLoggedIn();
            }
        );
    }

    auto AppContext::SetLoggedInUserName(const QString& name) -> void {
        if (m_logged_in_user_name == name) {
            return;
        }
        m_logged_in_user_name = name;
        emit loggedInUserNameChanged();
    }

    auto AppContext::UpdateIsLoggedIn() -> void {
        const bool loggedIn = m_token_store->HasValidAccessToken();
        if (m_is_logged_in == loggedIn) {
            return;
        }
        m_is_logged_in = loggedIn;
        emit isLoggedInChanged();
    }

} // namespace disk::qml::app
