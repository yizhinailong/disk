/**
 * @file AuthApi.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief AuthApi implementation
 * @version 0.1
 * @date 2026-03-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "AuthApi.hpp"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <dtos/AuthDtos.hpp>

#include "ApiClient.hpp"

namespace disk::qml::api {
    namespace {
        auto ParseEnvelopeFromReply(
            bool hasNetworkError,
            const QString& networkErrorString,
            const QByteArray& body,
            AuthApiCallback& cb
        ) -> void {
            if (hasNetworkError) {
                cb(models::ApiEnvelope{}, networkErrorString);
                return;
            }

            if (body.isEmpty()) {
                cb(models::ApiEnvelope{}, QStringLiteral("Failed to parse response JSON"));
                return;
            }

            QJsonParseError parseError;
            const QJsonDocument json = QJsonDocument::fromJson(body, &parseError);
            if (parseError.error != QJsonParseError::NoError) {
                cb(models::ApiEnvelope{}, QStringLiteral("Failed to parse response JSON"));
                return;
            }

            auto envelope = models::ParseEnvelope(json);
            if (!envelope) {
                cb(models::ApiEnvelope{}, QStringLiteral("Invalid envelope format"));
                return;
            }

            cb(std::move(*envelope), QString{});
        }
    } // namespace

    AuthApi::AuthApi(ApiClient* client)
        : m_client(client) {
    }

    auto AuthApi::Register(
        const QString& username,
        const QString& email,
        const QString& password,
        QObject* ctx,
        AuthApiCallback cb
    ) -> void {
        models::RegisterRequest dto;
        dto.username = username;
        dto.email = email;
        dto.password = password;
        const QJsonObject body = dto.ToJsonObject();
        m_client->PostJson(
            QStringLiteral("/api/auth/register"),
            body,
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    auto AuthApi::Login(
        const QString& account,
        const QString& password,
        QObject* ctx,
        AuthApiCallback cb
    ) -> void {
        models::LoginRequest dto;
        dto.account = account;
        dto.password = password;
        const QJsonObject body = dto.ToJsonObject();
        m_client->PostJson(
            QStringLiteral("/api/auth/login"),
            body,
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    auto AuthApi::Refresh(
        const QString& refreshToken,
        QObject* ctx,
        AuthApiCallback cb
    ) -> void {
        models::RefreshTokenRequest dto;
        dto.refreshToken = refreshToken;
        const QJsonObject body = dto.ToJsonObject();
        m_client->PostJson(
            QStringLiteral("/api/auth/refresh"),
            body,
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    auto AuthApi::Logout(
        const QString& accessToken,
        QObject* ctx,
        AuthApiCallback cb
    ) -> void {
        QJsonObject body; // empty body {}

        m_client->PostJsonWithBearerToken(
            QStringLiteral("/api/auth/logout"),
            body,
            accessToken,
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

} // namespace disk::qml::api
