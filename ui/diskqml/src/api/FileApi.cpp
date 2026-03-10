/**
 * @file FileApi.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief FileApi implementation
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "FileApi.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>

#include <dtos/ApiEnvelope.hpp>

#include "ApiClient.hpp"

namespace disk::qml::api {

    FileApi::FileApi(ApiClient* client)
        : m_client(client) {
    }

    auto FileApi::List(
        qint64 parentId,
        int page,
        int pageSize,
        const QString& sortBy,
        const QString& sortOrder,
        const QString& type,
        QObject* ctx,
        FileApiCallback cb
    ) -> void {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("parent_id"), QString::number(parentId));
        query.addQueryItem(QStringLiteral("page"), QString::number(page));
        query.addQueryItem(QStringLiteral("page_size"), QString::number(pageSize));
        query.addQueryItem(QStringLiteral("sort_by"), sortBy);
        query.addQueryItem(QStringLiteral("sort_order"), sortOrder);
        query.addQueryItem(QStringLiteral("type"), type);

        m_client->Get(
            QStringLiteral("/api/file/list"),
            query,
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    auto FileApi::GetDetail(qint64 fileId, QObject* ctx, FileApiCallback cb) -> void {
        m_client->Get(
            QStringLiteral("/api/file/%1").arg(fileId),
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    auto FileApi::DownloadInfo(qint64 fileId, QObject* ctx, FileApiCallback cb) -> void {
        m_client->Get(
            QStringLiteral("/api/file/download/%1/info").arg(fileId),
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    auto FileApi::Download(qint64 fileId, QObject* ctx, ApiReplyCallback cb) -> void {
        m_client->Get(
            QStringLiteral("/api/file/download/%1").arg(fileId),
            ctx,
            std::move(cb)
        );
    }

    auto FileApi::Rename(
        qint64 fileId,
        const QString& newName,
        QObject* ctx,
        FileApiCallback cb
    ) -> void {
        QJsonObject body;
        body.insert(QLatin1String("new_name"), newName);

        m_client->PutJson(
            QStringLiteral("/api/file/%1/rename").arg(fileId),
            body,
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    auto FileApi::Move(
        const QList<qint64>& fileIds,
        qint64 targetFolderId,
        QObject* ctx,
        FileApiCallback cb
    ) -> void {
        QJsonArray ids;
        for (auto id : fileIds) {
            ids.append(id);
        }

        QJsonObject body;
        body.insert(QLatin1String("file_ids"), ids);
        body.insert(QLatin1String("target_folder_id"), targetFolderId);

        m_client->PutJson(
            QStringLiteral("/api/file/move"),
            body,
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    auto FileApi::Copy(
        const QList<qint64>& fileIds,
        qint64 targetFolderId,
        QObject* ctx,
        FileApiCallback cb
    ) -> void {
        QJsonArray ids;
        for (auto id : fileIds) {
            ids.append(id);
        }

        QJsonObject body;
        body.insert(QLatin1String("file_ids"), ids);
        body.insert(QLatin1String("target_folder_id"), targetFolderId);

        m_client->PostJson(
            QStringLiteral("/api/file/copy"),
            body,
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    auto FileApi::Delete(
        const QList<qint64>& fileIds,
        QObject* ctx,
        FileApiCallback cb
    ) -> void {
        QJsonArray ids;
        for (auto id : fileIds) {
            ids.append(id);
        }

        QJsonObject body;
        body.insert(QLatin1String("file_ids"), ids);

        m_client->DeleteJson(
            QStringLiteral("/api/file"),
            body,
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    auto FileApi::Search(
        const QString& keyword,
        const QString& type,
        qint64 folderId,
        int page,
        int pageSize,
        QObject* ctx,
        FileApiCallback cb
    ) -> void {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("keyword"), keyword);
        query.addQueryItem(QStringLiteral("type"), type);
        if (folderId >= 0) {
            query.addQueryItem(QStringLiteral("folder_id"), QString::number(folderId));
        }
        query.addQueryItem(QStringLiteral("page"), QString::number(page));
        query.addQueryItem(QStringLiteral("page_size"), QString::number(pageSize));

        m_client->Get(
            QStringLiteral("/api/file/search"),
            query,
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

} // namespace disk::qml::api
