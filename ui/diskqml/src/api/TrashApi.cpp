/**
 * @file TrashApi.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief TrashApi 实现
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "TrashApi.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>

#include <dtos/ApiEnvelope.hpp>

#include "ApiClient.hpp"

namespace disk::qml::api {

    TrashApi::TrashApi(ApiClient* client)
        : m_client(client) {
    }

    auto TrashApi::List(
        int page,
        int pageSize,
        QObject* ctx,
        TrashApiCallback cb
    ) -> void {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("page"), QString::number(page));
        query.addQueryItem(QStringLiteral("page_size"), QString::number(pageSize));

        m_client->Get(
            QStringLiteral("/api/trash"),
            query,
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    auto TrashApi::Restore(
        const QList<qint64>& trashIds,
        QObject* ctx,
        TrashApiCallback cb
    ) -> void {
        QJsonArray ids;
        for (auto id : trashIds) {
            ids.append(id);
        }

        QJsonObject body;
        body.insert(QLatin1String("trash_ids"), ids);

        m_client->PostJson(
            QStringLiteral("/api/trash/restore"),
            body,
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    auto TrashApi::Delete(
        const QList<qint64>& trashIds,
        QObject* ctx,
        TrashApiCallback cb
    ) -> void {
        QJsonArray ids;
        for (auto id : trashIds) {
            ids.append(id);
        }

        QJsonObject body;
        body.insert(QLatin1String("trash_ids"), ids);

        m_client->DeleteJson(
            QStringLiteral("/api/trash"),
            body,
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

    auto TrashApi::ClearAll(QObject* ctx, TrashApiCallback cb) -> void {
        m_client->Delete(
            QStringLiteral("/api/trash/all"),
            ctx,
            [cb = std::move(cb)](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                models::ParseEnvelopeFromReply(hasNetworkError, networkErrorString, body, cb);
            }
        );
    }

} // namespace disk::qml::api
