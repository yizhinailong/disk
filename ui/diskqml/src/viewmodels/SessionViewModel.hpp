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

namespace disk::qml::viewmodels {

    class LoginViewModel;

    /// Session state singleton exposed to QML.
    /// Tracks logged-in status and username; provides logout action.
    class SessionViewModel : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_SINGLETON

        Q_PROPERTY(bool isLoggedIn READ IsLoggedIn NOTIFY isLoggedInChanged)
        Q_PROPERTY(QString loggedInUserName READ LoggedInUserName NOTIFY loggedInUserNameChanged)

    public:
        explicit SessionViewModel(
            LoginViewModel* loginViewModel,
            services::TokenStore* tokenStore,
            services::AuthService* authService,
            QObject* parent = nullptr
        );

        static auto SetInstance(SessionViewModel* instance) -> void;
        static auto create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> SessionViewModel*;

        [[nodiscard]] auto IsLoggedIn() const -> bool;
        [[nodiscard]] auto LoggedInUserName() const -> const QString&;

        Q_INVOKABLE void logout();

    signals:
        void isLoggedInChanged();
        void loggedInUserNameChanged();

    private:
        auto SetLoggedInUserName(const QString& name) -> void;
        auto UpdateIsLoggedIn() -> void;

        LoginViewModel* m_login_view_model;
        services::TokenStore* m_token_store;
        services::AuthService* m_auth_service;

        bool m_is_logged_in{ false };
        QString m_logged_in_user_name;

        inline static SessionViewModel* s_instance = nullptr;
        inline static QJSEngine* s_engine = nullptr;
    };

} // namespace disk::qml::viewmodels
