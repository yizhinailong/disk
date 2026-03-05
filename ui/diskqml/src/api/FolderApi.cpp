/**
 * @file FolderApi.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief FolderApi implementation
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "FolderApi.hpp"

#include <QJsonObject>
#include <QUrlQuery>

#include <dtos/ApiEnvelope.hpp>

#include "ApiClient.hpp"

namespace disk::qml::api {

    FolderApi::FolderApi(ApiClient* client)
        : m_client(client) {
    }

    auto FolderApi::CreateFolder(
        const QString& name,
        qint64 parentId,
        QObject* ctx,
        FolderApiCallback cb
    ) -> void {
        QJsonObject body;
        body.insert(QLatin1String("name"), name);
        body.insert(QLatin1String("parent_id"), parentId);

        m_client->PostJson(
            QStringLiteral("/api/folder/create"),
            body,
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    auto FolderApi::GetTree(
        qint64 parentId,
        int depth,
        QObject* ctx,
        FolderApiCallback cb
    ) -> void {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("parent_id"), QString::number(parentId));
        query.addQueryItem(QStringLiteral("depth"), QString::number(depth));

        m_client->Get(
            QStringLiteral("/api/folder/tree"),
            query,
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    auto FolderApi::GetBreadcrumb(qint64 folderId, QObject* ctx, FolderApiCallback cb) -> void {
        m_client->Get(
            QStringLiteral("/api/folder/%1/breadcrumb").arg(folderId),
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

} // namespace disk::qml::api
