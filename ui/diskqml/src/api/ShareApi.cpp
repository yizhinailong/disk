/**
 * @file ShareApi.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief ShareApi 实现
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "ShareApi.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrlQuery>

#include <dtos/ApiEnvelope.hpp>

#include "ApiClient.hpp"

namespace disk::qml::api {

    ShareApi::ShareApi(ApiClient* client)
        : m_client(client) {
    }

    // ==================== 所有者端点（JWT 认证） ====================

    auto ShareApi::Create(
        const QList<qint64>& fileIds,
        int expireDays,
        const QString& password,
        const QString& permission,
        QObject* ctx,
        ShareApiCallback cb
    ) -> void {
        QJsonArray ids;
        for (auto id : fileIds) {
            ids.append(id);
        }

        QJsonObject body;
        body.insert(QLatin1String("file_ids"), ids);
        body.insert(QLatin1String("expire_days"), expireDays);

        if (!password.isEmpty()) {
            body.insert(QLatin1String("password"), password);
        }

        body.insert(QLatin1String("permission"), permission);

        m_client->PostJson(
            QStringLiteral("/api/share"),
            body,
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    auto ShareApi::List(
        const QString& status,
        int page,
        int pageSize,
        QObject* ctx,
        ShareApiCallback cb
    ) -> void {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("status"), status);
        query.addQueryItem(QStringLiteral("page"), QString::number(page));
        query.addQueryItem(QStringLiteral("page_size"), QString::number(pageSize));

        m_client->Get(
            QStringLiteral("/api/share"),
            query,
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    auto ShareApi::GetDetail(const QString& shareId, QObject* ctx, ShareApiCallback cb) -> void {
        m_client->Get(
            QStringLiteral("/api/share/%1").arg(shareId),
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    auto ShareApi::Update(
        const QString& shareId,
        int expireDays,
        const std::optional<QString>& password,
        const QString& permission,
        QObject* ctx,
        ShareApiCallback cb
    ) -> void {
        QJsonObject body;

        if (expireDays >= 0) {
            body.insert(QLatin1String("expire_days"), expireDays);
        }

        if (password.has_value()) {
            body.insert(QLatin1String("password"), *password);
        }

        if (!permission.isEmpty()) {
            body.insert(QLatin1String("permission"), permission);
        }

        m_client->PutJson(
            QStringLiteral("/api/share/%1").arg(shareId),
            body,
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    auto ShareApi::Cancel(
        const QStringList& shareIds,
        QObject* ctx,
        ShareApiCallback cb
    ) -> void {
        QJsonArray ids;
        for (const auto& id : shareIds) {
            ids.append(id);
        }

        QJsonObject body;
        body.insert(QLatin1String("share_ids"), ids);

        m_client->DeleteJson(
            QStringLiteral("/api/share"),
            body,
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    // ==================== 公开端点（无需认证） ====================

    auto ShareApi::Access(
        const QString& shareId,
        const QString& password,
        QObject* ctx,
        ShareApiCallback cb
    ) -> void {
        QJsonObject body;
        if (!password.isEmpty()) {
            body.insert(QLatin1String("password"), password);
        }

        m_client->PostJson(
            QStringLiteral("/api/share/access/%1").arg(shareId),
            body,
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    // ==================== 分享令牌端点（X-Share-Token） ====================

    auto ShareApi::Browse(
        const QString& shareId,
        const QString& shareToken,
        qint64 folderId,
        QObject* ctx,
        ShareApiCallback cb
    ) -> void {
        QString path = QStringLiteral("/api/share/browse/%1").arg(shareId);

        QUrlQuery query;
        if (folderId >= 0) {
            query.addQueryItem(QStringLiteral("folder_id"), QString::number(folderId));
        }

        auto req = m_client->CreateStreamingRequest(path);
        if (query.hasQueryItem(QStringLiteral("folder_id"))) {
            QUrl url = req.url();
            url.setQuery(query);
            req.setUrl(url);
        }
        req.setRawHeader(QByteArrayLiteral("X-Share-Token"), shareToken.toUtf8());

        auto* nam = m_client->NetworkAccessManager();
        auto* reply = nam->get(req);

        QObject::connect(reply, &QNetworkReply::finished, ctx, [reply, cb = std::move(cb)]() mutable {
            reply->deleteLater();

            const bool hasNetworkError = reply->error() != QNetworkReply::NoError;
            const QString errorString = hasNetworkError ? reply->errorString() : QString{};
            const QByteArray body = reply->readAll();

            models::ParseEnvelopeFromReply(hasNetworkError, errorString, body, cb);
        });
    }

    auto ShareApi::Download(
        const QString& shareId,
        qint64 fileId,
        const QString& shareToken,
        QObject* ctx,
        ApiReplyCallback cb
    ) -> void {
        QString path = QStringLiteral("/api/share/download/%1/%2").arg(shareId).arg(fileId);

        auto req = m_client->CreateStreamingRequest(path);
        req.setRawHeader(QByteArrayLiteral("X-Share-Token"), shareToken.toUtf8());

        auto* nam = m_client->NetworkAccessManager();
        auto* reply = nam->get(req);

        QObject::connect(reply, &QNetworkReply::finished, ctx, [reply, cb = std::move(cb)]() mutable {
            reply->deleteLater();

            const bool hasNetworkError = reply->error() != QNetworkReply::NoError;
            const QString errorString = hasNetworkError ? reply->errorString() : QString{};
            const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray body = reply->readAll();

            cb(hasNetworkError, errorString, httpStatus, body);
        });
    }

} // namespace disk::qml::api
