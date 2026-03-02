#pragma once

#include "IAuthApi.hpp"

namespace disk::qml::api {

    class IApiClient;

    /// Concrete auth API implementation that delegates to IApiClient::PostJson.
    class AuthApi final : public IAuthApi {
    public:
        /// @param client  Pointer to the API client (must outlive this object)
        explicit AuthApi(IApiClient* client);

        auto Register(const QString& username, const QString& email, const QString& password, QObject* ctx, AuthApiCallback cb) -> void override;

        auto Login(const QString& account, const QString& password, QObject* ctx, AuthApiCallback cb) -> void override;

        auto Refresh(const QString& refreshToken, QObject* ctx, AuthApiCallback cb) -> void override;

        auto Logout(const QString& accessToken, QObject* ctx, AuthApiCallback cb) -> void override;

    private:
        IApiClient* m_client;
    };

} // namespace disk::qml::api
