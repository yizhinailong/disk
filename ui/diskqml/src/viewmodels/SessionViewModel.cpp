/**
 * @file SessionViewModel.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief SessionViewModel implementation
 * @version 0.1
 * @date 2026-03-02
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "SessionViewModel.hpp"

#include <QJSEngine>
#include <QQmlEngine>

#include <services/AuthService.hpp>
#include <services/TokenStore.hpp>
#include <viewmodels/LoginViewModel.hpp>

namespace disk::qml::viewmodels {

    SessionViewModel::SessionViewModel(
        LoginViewModel* loginViewModel,
        services::TokenStore* tokenStore,
        services::AuthService* authService,
        QObject* parent
    ) : QObject(parent),
        m_login_view_model(loginViewModel),
        m_token_store(tokenStore),
        m_auth_service(authService) {
        // Initialize logged-in state from existing token
        m_is_logged_in = m_token_store->HasValidAccessToken();

        // Connect login success → update username and logged-in state
        connect(
            m_login_view_model,
            &LoginViewModel::loginSucceeded,
            this,
            [this](const QString& username) {
                SetLoggedInUserName(username);
                UpdateIsLoggedIn();
            }
        );
    }

    auto SessionViewModel::IsLoggedIn() const -> bool {
        return m_is_logged_in;
    }

    auto SessionViewModel::LoggedInUserName() const -> const QString& {
        return m_logged_in_user_name;
    }

    void SessionViewModel::logout() {
        const QString accessToken = m_token_store->AccessToken();

        // Context object prevents callback invocation if SessionViewModel is destroyed
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

    auto SessionViewModel::SetLoggedInUserName(const QString& name) -> void {
        if (m_logged_in_user_name == name) {
            return;
        }
        m_logged_in_user_name = name;
        emit loggedInUserNameChanged();
    }

    auto SessionViewModel::UpdateIsLoggedIn() -> void {
        const bool loggedIn = m_token_store->HasValidAccessToken();
        if (m_is_logged_in == loggedIn) {
            return;
        }
        m_is_logged_in = loggedIn;
        emit isLoggedInChanged();
    }

    auto SessionViewModel::SetInstance(SessionViewModel* instance) -> void {
        s_instance = instance;
    }

    auto SessionViewModel::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> SessionViewModel* {
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
