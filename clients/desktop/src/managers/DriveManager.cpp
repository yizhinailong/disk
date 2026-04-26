#include "DriveManager.hpp"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrlQuery>

namespace disk::desktop::managers {

    DriveManager::DriveManager(
        disk::desktop::NetworkClient* networkClient,
        disk::desktop::RequestFactory* requestFactory,
        QObject* parent
    )
        : QObject(parent),
          m_listModel(new disk::desktop::DriveListModel(this)),
          m_treeModel(new disk::desktop::FolderTreeModel(this)),
          m_networkClient(networkClient),
          m_requestFactory(requestFactory),
          m_delete_nam(this) {}

    DriveManager::~DriveManager() {
        for (auto* reply : m_active_replies) {
            if (reply) {
                reply->abort();
                reply->deleteLater();
            }
        }
    }

    disk::desktop::DriveListModel* DriveManager::listModel() const {
        return m_listModel;
    }

    disk::desktop::FolderTreeModel* DriveManager::treeModel() const {
        return m_treeModel;
    }

    auto DriveManager::PrepareHeaders() -> QMap<QString, QString> {
        return m_requestFactory->PrepareHeaders(disk::desktop::AuthDomain::Owner);
    }

    auto DriveManager::ParseJsonResponse(QNetworkReply* reply) -> std::optional<QJsonObject> {
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

    auto DriveManager::BuildApiError(QNetworkReply* reply) -> disk::desktop::ApiError {
        if (!reply) {
            disk::desktop::ApiError err;
            err.code = 0;
            err.message = "Network error: null reply";
            return err;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (json_opt.has_value() && json_opt->contains("error")) {
            return ErrorAdapter::FromJson(json_opt->value("error").toObject());
        }
        if (json_opt.has_value() && json_opt->value("code").toInt(0) != 0) {
            return ErrorAdapter::FromJson(*json_opt);
        }

        return ErrorAdapter::FromNetworkError(reply->error());
    }

    void DriveManager::EmitApiError(QNetworkReply* reply) {
        auto err = BuildApiError(reply);
        emit apiError(err.message, err.code);
    }

    void DriveManager::listFiles(const QString& parentId, int page, int pageSize, const QString& sort, const QString& typeFilter) {
        QUrlQuery query;
        if (!parentId.isEmpty()) {
            query.addQueryItem("parent_id", parentId);
        }
        query.addQueryItem("page", QString::number(page));
        query.addQueryItem("page_size", QString::number(pageSize));
        if (!sort.isEmpty()) {
            auto separator_index = sort.lastIndexOf('_');
            auto sort_by = separator_index > 0 ? sort.left(separator_index) : sort;
            auto sort_order = separator_index > 0 ? sort.mid(separator_index + 1) : QString("asc");
            query.addQueryItem("sort_by", sort_by);
            query.addQueryItem("sort_order", sort_order);
        }
        if (!typeFilter.isEmpty()) {
            query.addQueryItem("type", typeFilter);
        }

        QUrl url("/api/file/list");
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

    void DriveManager::searchFiles(const QString& query) {
        QUrlQuery urlQuery;
        urlQuery.addQueryItem("keyword", query);

        QUrl url("/api/file/search");
        url.setQuery(urlQuery);

        auto headers = PrepareHeaders();
        auto* reply = m_networkClient->Get(url, headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleSearchResponse(reply);
        });
    }

    void DriveManager::getFileDetail(const QString& fileId) {
        QUrl url(QString("/api/file/%1").arg(fileId));
        auto headers = PrepareHeaders();
        auto* reply = m_networkClient->Get(url, headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleDetailResponse(reply);
        });
    }

    void DriveManager::createFolder(const QString& parentId, const QString& name) {
        QJsonObject body;
        body["parent_id"] = parentId.isEmpty() ? QString("0") : parentId;
        body["name"] = name;

        QJsonDocument doc(body);
        auto headers = PrepareHeaders();
        headers["Content-Type"] = "application/json";

        auto* reply = m_networkClient->Post(QUrl("/api/folder/create"), doc.toJson(QJsonDocument::Compact), headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleCreateFolderResponse(reply);
        });
    }

    void DriveManager::renameItem(const QString& fileId, const QString& newName) {
        QJsonObject body;
        body["new_name"] = newName;

        QJsonDocument doc(body);
        auto headers = PrepareHeaders();
        headers["Content-Type"] = "application/json";

        QUrl url(QString("/api/file/%1/rename").arg(fileId));
        auto* reply = m_networkClient->Put(url, doc.toJson(QJsonDocument::Compact), headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleRenameResponse(reply);
        });
    }

    void DriveManager::moveItems(const QStringList& fileIds, const QString& targetParentId) {
        QJsonObject body;
        QJsonArray ids;
        for (const auto& id : fileIds) {
            bool ok = false;
            auto val = id.toULongLong(&ok);
            if (ok) {
                ids.append(static_cast<double>(val));
            }
        }
        body["file_ids"] = ids;
        body["target_parent_id"] = targetParentId.isEmpty() ? QString("0") : targetParentId;

        QJsonDocument doc(body);
        auto headers = PrepareHeaders();
        headers["Content-Type"] = "application/json";

        auto* reply = m_networkClient->Put(QUrl("/api/file/move"), doc.toJson(QJsonDocument::Compact), headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleMoveResponse(reply);
        });
    }

    void DriveManager::copyItems(const QStringList& fileIds, const QString& targetParentId) {
        QJsonObject body;
        QJsonArray ids;
        for (const auto& id : fileIds) {
            bool ok = false;
            auto val = id.toULongLong(&ok);
            if (ok) {
                ids.append(static_cast<double>(val));
            }
        }
        body["file_ids"] = ids;
        body["target_parent_id"] = targetParentId.isEmpty() ? QString("0") : targetParentId;

        QJsonDocument doc(body);
        auto headers = PrepareHeaders();
        headers["Content-Type"] = "application/json";

        auto* reply = m_networkClient->Post(QUrl("/api/file/copy"), doc.toJson(QJsonDocument::Compact), headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleCopyResponse(reply);
        });
    }

    void DriveManager::deleteItems(const QStringList& fileIds) {
        QJsonObject body;
        QJsonArray ids;
        for (const auto& id : fileIds) {
            bool ok = false;
            auto val = id.toULongLong(&ok);
            if (ok) {
                ids.append(static_cast<double>(val));
            }
        }
        body["file_ids"] = ids;

        QByteArray json_body = QJsonDocument(body).toJson(QJsonDocument::Compact);

        QString base_url = m_networkClient->GetBaseUrl();
        QString path = base_url + "api/file";
        QUrl url(path);

        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setHeader(QNetworkRequest::UserAgentHeader, "DiskDesktop/1.0");

        auto headers = PrepareHeaders();
        for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
            request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
        }

        auto* reply = m_delete_nam.sendCustomRequest(request, "DELETE", json_body);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleDeleteResponse(reply);
        });
    }

    void DriveManager::loadFolderTree() {
        auto headers = PrepareHeaders();
        auto* reply = m_networkClient->Get(QUrl("/api/folder/tree"), headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleTreeResponse(reply);
        });
    }

    void DriveManager::loadBreadcrumb(const QString& folderId) {
        if (folderId.isEmpty() || folderId == "0") {
            QVariantMap root;
            root["id"] = 0.0;
            root["name"] = QStringLiteral("根目录");
            emit breadcrumbLoaded(QVariantList{ root });
            return;
        }

        QUrl url(QString("/api/folder/%1/breadcrumb").arg(folderId));
        auto headers = PrepareHeaders();
        auto* reply = m_networkClient->Get(url, headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleBreadcrumbResponse(reply);
        });
    }

    // ── Response handlers ──

    void DriveManager::HandleListResponse(QNetworkReply* reply) {
        if (!reply) {
            auto message = QStringLiteral("Network error: null reply");
            emit apiError(message, 0);
            emit listLoadFailed(message, 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            auto err = BuildApiError(reply);
            emit apiError(err.message, err.code);
            emit listLoadFailed(err.message, err.code);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            auto message = QStringLiteral("Invalid response format");
            emit apiError(message, 0);
            emit listLoadFailed(message, 0);
            return;
        }

        if (json_opt->value("code").toInt(0) != 0) {
            auto err = ErrorAdapter::FromJson(*json_opt);
            emit apiError(err.message, err.code);
            emit listLoadFailed(err.message, err.code);
            return;
        }

        auto data = json_opt->value("data").toObject();
        auto items_array = data.value("items").toArray();

        QVector<disk::desktop::DriveItem> items;
        items.reserve(items_array.size());
        for (const auto& val : items_array) {
            items.append(disk::desktop::DriveItem::FromJson(val.toObject(), "file_list"));
        }

        m_listModel->SetItems(items);

        auto pagination = data.value("pagination").toObject();
        int page = pagination.value("page").toInt(1);
        int total_pages = pagination.value("total_pages").toInt(1);
        int total = pagination.value("total").toInt(0);
        emit paginationLoaded(page, total_pages, total);

        if (items.isEmpty()) {
            emit operationSuccess("Empty folder");
        }
    }

    void DriveManager::HandleSearchResponse(QNetworkReply* reply) {
        if (!reply) {
            auto message = QStringLiteral("Network error: null reply");
            emit apiError(message, 0);
            emit listLoadFailed(message, 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            auto err = BuildApiError(reply);
            emit apiError(err.message, err.code);
            emit listLoadFailed(err.message, err.code);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            auto message = QStringLiteral("Invalid response format");
            emit apiError(message, 0);
            emit listLoadFailed(message, 0);
            return;
        }

        if (json_opt->value("code").toInt(0) != 0) {
            auto err = ErrorAdapter::FromJson(*json_opt);
            emit apiError(err.message, err.code);
            emit listLoadFailed(err.message, err.code);
            return;
        }

        auto data = json_opt->value("data").toObject();
        auto items_array = data.value("items").toArray();

        QVector<disk::desktop::DriveItem> items;
        items.reserve(items_array.size());
        for (const auto& val : items_array) {
            items.append(disk::desktop::DriveItem::FromJson(val.toObject(), "search"));
        }

        m_listModel->SetItems(items);

        auto pagination = data.value("pagination").toObject();
        int page = pagination.value("page").toInt(1);
        int total_pages = pagination.value("total_pages").toInt(1);
        int total = pagination.value("total").toInt(0);
        emit paginationLoaded(page, total_pages, total);
    }

    void DriveManager::HandleDetailResponse(QNetworkReply* reply) {
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
        QVariantMap detail;

        detail["id"] = static_cast<double>(data.value("id").toDouble(0));
        detail["name"] = data.value("name").toString();
        detail["type"] = data.value("type").toString();
        detail["parent_id"] = static_cast<double>(data.value("parent_id").toDouble(0));

        if (data.contains("size")) {
            detail["size"] = static_cast<double>(data.value("size").toDouble(0));
        }
        if (data.contains("mime_type")) {
            detail["mime_type"] = data.value("mime_type").toString();
        }
        if (data.contains("hash")) {
            detail["hash"] = data.value("hash").toString();
        }
        if (data.contains("item_count")) {
            detail["item_count"] = data.value("item_count").toInt();
        }
        if (data.contains("path")) {
            detail["path"] = data.value("path").toString();
        }
        if (data.contains("created_at")) {
            detail["created_at"] = data.value("created_at").toString();
        }
        if (data.contains("updated_at")) {
            detail["updated_at"] = data.value("updated_at").toString();
        }

        emit fileDetailLoaded(detail);
    }

    void DriveManager::HandleCreateFolderResponse(QNetworkReply* reply) {
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

        if (json_opt->value("code").toInt(0) != 0) {
            auto err = ErrorAdapter::FromJson(*json_opt);
            emit apiError(err.message, err.code);
            return;
        }

        emit operationSuccess("Folder created");
    }

    void DriveManager::HandleRenameResponse(QNetworkReply* reply) {
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

        if (json_opt->value("code").toInt(0) != 0) {
            auto err = ErrorAdapter::FromJson(*json_opt);
            emit apiError(err.message, err.code);
            return;
        }

        emit operationSuccess("Item renamed");
    }

    void DriveManager::HandleMoveResponse(QNetworkReply* reply) {
        if (!reply) {
            emit apiError("Network error: null reply", 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        emit operationSuccess("Items moved");
    }

    void DriveManager::HandleCopyResponse(QNetworkReply* reply) {
        if (!reply) {
            emit apiError("Network error: null reply", 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        emit operationSuccess("Items copied");
    }

    void DriveManager::HandleDeleteResponse(QNetworkReply* reply) {
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

        if (json_opt->value("code").toInt(0) != 0) {
            auto err = ErrorAdapter::FromJson(*json_opt);
            emit apiError(err.message, err.code);
            return;
        }

        emit operationSuccess("Items deleted");
    }

    void DriveManager::HandleTreeResponse(QNetworkReply* reply) {
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

        disk::desktop::FolderNode root;
        root.id = 0;
        root.name = "My Drive";

        if (data.contains("children") && data["children"].isArray()) {
            const auto arr = data["children"].toArray();
            root.children.reserve(arr.size());
            for (const auto& child : arr) {
                root.children.append(disk::desktop::FolderNode::FromJson(child.toObject()));
            }
        }

        m_treeModel->SetRoot(root);
        emit operationSuccess("Folder tree loaded");
    }

    void DriveManager::HandleBreadcrumbResponse(QNetworkReply* reply) {
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
        auto items = data.value("path").toArray();

        QVariantList breadcrumb;
        for (const auto& val : items) {
            auto obj = val.toObject();
            QVariantMap entry;
            entry["id"] = static_cast<double>(obj.value("id").toDouble(0));
            entry["name"] = obj.value("name").toString();
            breadcrumb.append(entry);
        }

        emit breadcrumbLoaded(breadcrumb);
    }

} // namespace disk::desktop::managers
