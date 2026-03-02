#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace disk::qml::utils {
    class ConfigStore;
}

namespace disk::qml::storage {
    class TokenStore;
}

namespace disk::qml::api {
    class ApiClient;
    class AuthApi;
} // namespace disk::qml::api

namespace disk::qml::services {
    class AuthService;
}

namespace disk::qml::viewmodels {
    class LoginViewModel;
    class RegisterViewModel;
} // namespace disk::qml::viewmodels

class QQmlEngine;
class QJSEngine;

namespace disk::qml::app {

    /// Top-level DI aggregation object.
    /// Owns all services and viewmodels, exposing them to QML via Q_PROPERTY.
    /// Registered as a QML singleton instance in main.cpp.
    class AppContext : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_SINGLETON

        Q_PROPERTY(QObject* loginViewModel READ LoginViewModel CONSTANT)
        Q_PROPERTY(QObject* registerViewModel READ RegisterViewModel CONSTANT)
        Q_PROPERTY(bool isLoggedIn READ IsLoggedIn NOTIFY isLoggedInChanged)
        Q_PROPERTY(QString loggedInUserName READ LoggedInUserName NOTIFY loggedInUserNameChanged)

    public:
        explicit AppContext(
            utils::ConfigStore* configStore,
            storage::TokenStore* tokenStore,
            api::ApiClient* apiClient,
            api::AuthApi* authApi,
            services::AuthService* authService,
            viewmodels::LoginViewModel* loginViewModel,
            viewmodels::RegisterViewModel* registerViewModel,
            QObject* parent = nullptr
        );

        static auto SetInstance(AppContext* instance) -> void;
        static auto create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> AppContext*;

        [[nodiscard]] auto LoginViewModel() const -> QObject*;
        [[nodiscard]] auto RegisterViewModel() const -> QObject*;
        [[nodiscard]] auto IsLoggedIn() const -> bool;
        [[nodiscard]] auto LoggedInUserName() const -> const QString&;

        Q_INVOKABLE void logout();

    signals:
        void isLoggedInChanged();
        void loggedInUserNameChanged();

    private:
        auto SetLoggedInUserName(const QString& name) -> void;
        auto UpdateIsLoggedIn() -> void;

        utils::ConfigStore* m_config_store;
        storage::TokenStore* m_token_store;
        api::ApiClient* m_api_client;
        api::AuthApi* m_auth_api;
        services::AuthService* m_auth_service;
        viewmodels::LoginViewModel* m_login_view_model;
        viewmodels::RegisterViewModel* m_register_view_model;

        bool m_is_logged_in{ false };
        QString m_logged_in_user_name;

        inline static AppContext* s_instance = nullptr;
        inline static QJSEngine* s_engine = nullptr;
    };

} // namespace disk::qml::app
