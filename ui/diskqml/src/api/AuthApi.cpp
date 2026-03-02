#include "AuthApi.hpp"

#include <QJsonDocument>
#include <QJsonObject>

#include "IApiClient.hpp"

namespace disk::qml::api {
    AuthApi::AuthApi(IApiClient* client)
        : m_client(client) {
    }

    auto AuthApi::Register(
        const QString& username,
        const QString& email,
        const QString& password,
        QObject* ctx,
        AuthApiCallback cb
    ) -> void {
        QJsonObject body;
        body[QLatin1String("username")] = username;
        body[QLatin1String("email")] = email;
        body[QLatin1String("password")] = password;

        m_client->PostJson(
            QStringLiteral("/api/auth/register"),
            body,
            ctx,
            [cb = std::move(cb)](std::optional<QJsonDocument> json, QString networkError) {
                if (!networkError.isEmpty()) {
                    cb(models::ApiEnvelope{}, std::move(networkError));
                    return;
                }
                if (!json) {
                    cb(models::ApiEnvelope{}, QStringLiteral("Failed to parse response JSON"));
                    return;
                }
                auto envelope = models::ParseEnvelope(*json);
                if (!envelope) {
                    cb(models::ApiEnvelope{}, QStringLiteral("Invalid envelope format"));
                    return;
                }
                cb(std::move(*envelope), QString{});
            }
        );
    }

    auto AuthApi::Login(
        const QString& account,
        const QString& password,
        QObject* ctx,
        AuthApiCallback cb
    ) -> void {
        QJsonObject body;
        body[QLatin1String("account")] = account;
        body[QLatin1String("password")] = password;

        m_client->PostJson(
            QStringLiteral("/api/auth/login"),
            body,
            ctx,
            [cb = std::move(cb)](std::optional<QJsonDocument> json, QString networkError) {
                if (!networkError.isEmpty()) {
                    cb(models::ApiEnvelope{}, std::move(networkError));
                    return;
                }
                if (!json) {
                    cb(models::ApiEnvelope{}, QStringLiteral("Failed to parse response JSON"));
                    return;
                }
                auto envelope = models::ParseEnvelope(*json);
                if (!envelope) {
                    cb(models::ApiEnvelope{}, QStringLiteral("Invalid envelope format"));
                    return;
                }
                cb(std::move(*envelope), QString{});
            }
        );
    }

    auto AuthApi::Refresh(
        const QString& refreshToken,
        QObject* ctx,
        AuthApiCallback cb
    ) -> void {
        QJsonObject body;
        body[QLatin1String("refresh_token")] = refreshToken;

        m_client->PostJson(
            QStringLiteral("/api/auth/refresh"),
            body,
            ctx,
            [cb = std::move(cb)](std::optional<QJsonDocument> json, QString networkError) {
                if (!networkError.isEmpty()) {
                    cb(models::ApiEnvelope{}, std::move(networkError));
                    return;
                }
                if (!json) {
                    cb(models::ApiEnvelope{}, QStringLiteral("Failed to parse response JSON"));
                    return;
                }
                auto envelope = models::ParseEnvelope(*json);
                if (!envelope) {
                    cb(models::ApiEnvelope{}, QStringLiteral("Invalid envelope format"));
                    return;
                }
                cb(std::move(*envelope), QString{});
            }
        );
    }

    auto AuthApi::Logout(
        const QString& accessToken,
        QObject* ctx,
        AuthApiCallback cb
    ) -> void {
        // Set the bearer token for Authorization header before issuing the request
        m_client->SetBearerToken(accessToken);

        QJsonObject body; // empty body {}

        m_client->PostJson(
            QStringLiteral("/api/auth/logout"),
            body,
            ctx,
            [cb = std::move(cb)](std::optional<QJsonDocument> json, QString networkError) {
                if (!networkError.isEmpty()) {
                    cb(models::ApiEnvelope{}, std::move(networkError));
                    return;
                }
                if (!json) {
                    cb(models::ApiEnvelope{}, QStringLiteral("Failed to parse response JSON"));
                    return;
                }
                auto envelope = models::ParseEnvelope(*json);
                if (!envelope) {
                    cb(models::ApiEnvelope{}, QStringLiteral("Invalid envelope format"));
                    return;
                }
                cb(std::move(*envelope), QString{});
            }
        );
    }

} // namespace disk::qml::api
