#pragma once

#include <QObject>
#include <QString>
#include <functional>

#include <models/AuthDtos.hpp>

namespace disk::qml::api {

    using AuthApiCallback = std::function<void(models::ApiEnvelope envelope, QString networkError)>;

    class ApiClient;

    class AuthApi {
    public:
        /// @param client  Pointer to the API client (must outlive this object)
        explicit AuthApi(ApiClient* client);

        virtual auto Register(
            const QString& username,
            const QString& email,
            const QString& password,
            QObject* ctx,
            AuthApiCallback cb
        ) -> void;

        virtual auto Login(const QString& account, const QString& password, QObject* ctx, AuthApiCallback cb) -> void;

        virtual auto Refresh(const QString& refreshToken, QObject* ctx, AuthApiCallback cb) -> void;

        virtual auto Logout(const QString& accessToken, QObject* ctx, AuthApiCallback cb) -> void;

        virtual ~AuthApi() = default;

    private:
        ApiClient* m_client;
    };

} // namespace disk::qml::api
