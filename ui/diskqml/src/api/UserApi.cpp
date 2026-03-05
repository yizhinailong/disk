/**
 * @file UserApi.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief UserApi implementation
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "UserApi.hpp"

#include <QJsonObject>

#include <dtos/ApiEnvelope.hpp>

#include "ApiClient.hpp"

namespace disk::qml::api {

    UserApi::UserApi(ApiClient* client)
        : m_client(client) {
    }

    auto UserApi::GetProfile(QObject* ctx, UserApiCallback cb) -> void {
        m_client->Get(
            QStringLiteral("/api/user/profile"),
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    auto UserApi::GetStorage(QObject* ctx, UserApiCallback cb) -> void {
        m_client->Get(
            QStringLiteral("/api/user/storage"),
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    auto UserApi::UpdateProfile(
        const QString& nickname,
        const QString& avatar,
        QObject* ctx,
        UserApiCallback cb
    ) -> void {
        QJsonObject body;
        if (!nickname.isEmpty()) {
            body.insert(QLatin1String("nickname"), nickname);
        }
        if (!avatar.isEmpty()) {
            body.insert(QLatin1String("avatar"), avatar);
        }

        m_client->PatchJson(
            QStringLiteral("/api/user/profile"),
            body,
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    auto UserApi::ChangePassword(
        const QString& oldPassword,
        const QString& newPassword,
        QObject* ctx,
        UserApiCallback cb
    ) -> void {
        QJsonObject body;
        body.insert(QLatin1String("old_password"), oldPassword);
        body.insert(QLatin1String("new_password"), newPassword);

        m_client->PutJson(
            QStringLiteral("/api/user/password"),
            body,
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

} // namespace disk::qml::api
