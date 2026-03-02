#pragma once

#include <QObject>
#include <QString>

namespace disk::qml::services {
    class AuthService;
}

namespace disk::qml::viewmodels {

    class LoginViewModel : public QObject {
        Q_OBJECT

        Q_PROPERTY(QString account READ Account WRITE SetAccount NOTIFY accountChanged)
        Q_PROPERTY(QString password READ Password WRITE SetPassword NOTIFY passwordChanged)
        Q_PROPERTY(bool loading READ Loading NOTIFY loadingChanged)
        Q_PROPERTY(QString errorMessage READ ErrorMessage NOTIFY errorMessageChanged)
        Q_PROPERTY(bool canSubmit READ CanSubmit NOTIFY canSubmitChanged)

    public:
        explicit LoginViewModel(services::AuthService* authService, QObject* parent = nullptr);

        [[nodiscard]] auto Account() const -> const QString&;
        [[nodiscard]] auto Password() const -> const QString&;
        [[nodiscard]] auto Loading() const -> bool;
        [[nodiscard]] auto ErrorMessage() const -> const QString&;
        [[nodiscard]] auto CanSubmit() const -> bool;

        auto SetAccount(const QString& account) -> void;
        auto SetPassword(const QString& password) -> void;

        Q_INVOKABLE void submit();
        Q_INVOKABLE void clearError();

    signals:
        void accountChanged();
        void passwordChanged();
        void loadingChanged();
        void errorMessageChanged();
        void canSubmitChanged();
        void loginSucceeded(const QString& username);

    private:
        auto UpdateCanSubmit() -> void;
        auto SetLoading(bool loading) -> void;
        auto SetErrorMessage(const QString& message) -> void;

        services::AuthService* m_auth_service;
        QString m_account;
        QString m_password;
        bool m_loading{ false };
        QString m_error_message;
        bool m_can_submit{ false };
    };

} // namespace disk::qml::viewmodels
