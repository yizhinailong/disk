/**
 * @file AuthApi.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief AuthApi 实现
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "AuthApi.hpp"

#include <QJsonObject>

#include <dtos/ApiEnvelope.hpp>

#include "ApiClient.hpp"

namespace disk::qml::api {

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
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
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
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
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
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    auto AuthApi::Logout(
        const QString& accessToken,
        QObject* ctx,
        AuthApiCallback cb
    ) -> void {
        QJsonObject body; // 空请求体 {}

        m_client->PostJsonWithBearerToken(
            QStringLiteral("/api/auth/logout"),
            body,
            accessToken,
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

} // namespace disk::qml::api
