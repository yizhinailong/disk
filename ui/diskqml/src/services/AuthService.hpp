#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <optional>

#include <models/AuthDtos.hpp>

namespace disk::qml::api {
    class IAuthApi;
}

namespace disk::qml::services {
    class TokenStore;
}

namespace disk::qml::services {

    class AuthService final {
    public:
        using RegisterCallback = std::function<void(std::optional<models::RegisterResultDto> result, QString errorMessage)>;
        using LoginCallback = std::function<void(std::optional<models::LoginResultDto> result, QString errorMessage)>;
        using RefreshCallback = std::function<void(std::optional<models::RefreshResultDto> result, QString errorMessage)>;
        using LogoutCallback = std::function<void(bool ok, QString errorMessage)>;

        AuthService(api::IAuthApi* authApi, TokenStore* tokenStore);

        auto ValidateUsername(const QString& username) const -> bool;
        auto ValidateEmail(const QString& email) const -> bool;
        auto ValidatePassword(const QString& password) const -> bool;

        auto Register(const QString& username, const QString& email, const QString& password, QObject* ctx, RegisterCallback cb) -> void;
        auto Login(const QString& account, const QString& password, QObject* ctx, LoginCallback cb) -> void;
        auto Refresh(const QString& refreshToken, QObject* ctx, RefreshCallback cb) -> void;
        auto Logout(const QString& accessToken, QObject* ctx, LogoutCallback cb) -> void;

    private:
        auto MapTransportError(const QString& networkError) const -> QString;
        auto MapEnvelopeError(const models::ApiEnvelope& envelope) const -> QString;
        auto IsLocalLogoutSuccessCode(int code) const -> bool;

        api::IAuthApi* m_auth_api;
        TokenStore* m_token_store;
    };

} // namespace disk::qml::services
