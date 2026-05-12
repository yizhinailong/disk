#include "TrashManager.hpp"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrlQuery>

namespace disk::desktop::managers {

    TrashManager::TrashManager(
        disk::desktop::NetworkClient* networkClient,
        disk::desktop::RequestFactory* requestFactory,
        QObject* parent
    )
        : QObject(parent),
          m_listModel(new disk::desktop::TrashListModel(this)),
          m_batchResultModel(new disk::desktop::BatchResultModel(this)),
          m_networkClient(networkClient),
          m_requestFactory(requestFactory) {}

    TrashManager::~TrashManager() {
        for (auto* reply : m_active_replies) {
            if (reply) {
                reply->abort();
                reply->deleteLater();
            }
        }
    }

    disk::desktop::TrashListModel* TrashManager::listModel() const {
        return m_listModel;
    }

    disk::desktop::BatchResultModel* TrashManager::batchResultModel() const {
        return m_batchResultModel;
    }

    auto TrashManager::PrepareHeaders() -> QMap<QString, QString> {
        return m_requestFactory->PrepareHeaders(disk::desktop::AuthDomain::Owner);
    }

    auto TrashManager::ParseJsonResponse(QNetworkReply* reply) -> std::optional<QJsonObject> {
        if (!reply) {
            return std::nullopt;
        }
        QByteArray data = reply->readAll();
        QJsonParseError error;
        auto doc = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError) {
            return std::nullopt;
        }
        if (!doc.isObject()) {
            return std::nullopt;
        }
        return doc.object();
    }

    void TrashManager::EmitApiError(QNetworkReply* reply) {
        if (!reply) {
            emit apiError("Network error: null reply", 0);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (json_opt.has_value() && json_opt->contains("error")) {
            auto err = ErrorAdapter::FromJson(json_opt->value("error").toObject());
            emit apiError(err.message, err.code);
        } else {
            auto err = ErrorAdapter::FromNetworkError(reply->error());
            emit apiError(err.message, err.code);
        }
    }

    void TrashManager::listTrash(int page, int pageSize) {
        QUrlQuery query;
        query.addQueryItem("page", QString::number(page));
        query.addQueryItem("page_size", QString::number(pageSize));

        QUrl url("/api/trash");
        url.setQuery(query);

        auto headers = PrepareHeaders();
        auto* reply = m_networkClient->Get(url, headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleListResponse(reply);
        });
    }

    void TrashManager::restoreItems(const QStringList& trashIds) {
        QJsonObject body;
        QJsonArray ids;
        for (const auto& id : trashIds) {
            bool ok = false;
            auto val = id.toULongLong(&ok);
            if (ok) {
                ids.append(static_cast<double>(val));
            }
        }
        body["trash_ids"] = ids;

        QJsonDocument doc(body);
        auto headers = PrepareHeaders();
        headers["Content-Type"] = "application/json";

        auto* reply = m_networkClient->Post(
            QUrl("/api/trash/restore"),
            doc.toJson(QJsonDocument::Compact),
            headers
        );
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleRestoreResponse(reply);
        });
    }

    void TrashManager::deleteItems(const QStringList& trashIds) {
        QJsonObject body;
        QJsonArray ids;
        for (const auto& id : trashIds) {
            bool ok = false;
            auto val = id.toULongLong(&ok);
            if (ok) {
                ids.append(static_cast<double>(val));
            }
        }
        body["trash_ids"] = ids;

        QByteArray json_body = QJsonDocument(body).toJson(QJsonDocument::Compact);

        auto headers = PrepareHeaders();
        auto* reply = m_networkClient->Delete(QUrl("/api/trash"), json_body, headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleDeleteResponse(reply);
        });
    }

    void TrashManager::clearAll() {
        auto headers = PrepareHeaders();
        auto* reply = m_networkClient->Delete(QUrl("/api/trash/all"), QByteArray(), headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleClearAllResponse(reply);
        });
    }

    // ── Response Handlers ──

    void TrashManager::HandleListResponse(QNetworkReply* reply) {
        if (!reply) {
            emit apiError("Network error: null reply", 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            emit apiError("Invalid response format", 0);
            return;
        }

        auto data = json_opt->value("data").toObject();
        auto items_array = data.value("items").toArray();

        QVector<disk::desktop::TrashItem> items;
        items.reserve(items_array.size());
        for (const auto& val : items_array) {
            items.append(disk::desktop::TrashItem::FromJson(val.toObject()));
        }

        m_listModel->SetItems(items);

        auto pagination = data.value("pagination").toObject();
        int page = pagination.value("page").toInt(1);
        int total_pages = pagination.value("total_pages").toInt(1);
        int total = pagination.value("total").toInt(0);
        emit paginationLoaded(page, total_pages, total);
    }

    void TrashManager::HandleRestoreResponse(QNetworkReply* reply) {
        if (!reply) {
            emit apiError("Network error: null reply", 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            emit apiError("Invalid response format", 0);
            return;
        }

        auto data = json_opt->value("data").toObject();
        auto result = disk::desktop::BatchActionResult::FromJson(data, "trash_restore");
        m_batchResultModel->SetResult(result);
        emit batchResultReady();

        if (result.failure_count == 0) {
            emit operationSuccess("All items restored");
        } else if (result.success_count > 0) {
            emit operationSuccess(
                QString("Restored %1 of %2 items").arg(result.success_count).arg(result.total_count)
            );
        }
    }

    void TrashManager::HandleDeleteResponse(QNetworkReply* reply) {
        if (!reply) {
            emit apiError("Network error: null reply", 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            emit apiError("Invalid response format", 0);
            return;
        }

        auto data = json_opt->value("data").toObject();
        auto result = disk::desktop::BatchActionResult::FromJson(data, "trash_delete");
        m_batchResultModel->SetResult(result);
        emit batchResultReady();

        if (result.failure_count == 0) {
            emit operationSuccess("All items permanently deleted");
        } else if (result.success_count > 0) {
            emit operationSuccess(
                QString("Deleted %1 of %2 items").arg(result.success_count).arg(result.total_count)
            );
        }
    }

    void TrashManager::HandleClearAllResponse(QNetworkReply* reply) {
        if (!reply) {
            emit apiError("Network error: null reply", 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            emit apiError("Invalid response format", 0);
            return;
        }

        auto data = json_opt->value("data").toObject();
        int deleted_count = data.value("deleted_count").toInt(0);
        quint64 freed_space = static_cast<quint64>(data.value("freed_space").toDouble(0));

        emit clearAllCompleted(deleted_count, freed_space);
        emit operationSuccess(
            QString("Cleared %1 items").arg(deleted_count)
        );
    }

} // namespace disk::desktop::managers
