#include "ShareManager.hpp"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrlQuery>

#include "utils/ShareCodeParser.hpp"

namespace disk::desktop::managers {

    ShareManager::ShareManager(
        disk::desktop::NetworkClient* networkClient,
        disk::desktop::RequestFactory* requestFactory,
        QObject* parent
    )
        : QObject(parent),
          m_listModel(new disk::desktop::ShareListModel(this)),
          m_browseModel(new disk::desktop::DriveListModel(this)),
          m_batchResultModel(new disk::desktop::BatchResultModel(this)),
          m_networkClient(networkClient),
          m_requestFactory(requestFactory) {}

    ShareManager::~ShareManager() {
        for (auto* reply : m_active_replies) {
            if (reply) {
                reply->abort();
                reply->deleteLater();
            }
        }
    }

    disk::desktop::ShareListModel* ShareManager::listModel() const {
        return m_listModel;
    }

    disk::desktop::DriveListModel* ShareManager::browseModel() const {
        return m_browseModel;
    }

    disk::desktop::BatchResultModel* ShareManager::batchResultModel() const {
        return m_batchResultModel;
    }

    auto ShareManager::PrepareOwnerHeaders() -> QMap<QString, QString> {
        return m_requestFactory->PrepareHeaders(disk::desktop::AuthDomain::Owner);
    }

    auto ShareManager::PrepareVisitorHeaders() -> QMap<QString, QString> {
        return m_requestFactory->PrepareHeaders(disk::desktop::AuthDomain::Visitor);
    }

    auto ShareManager::ParseJsonResponse(QNetworkReply* reply) -> std::optional<QJsonObject> {
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

    void ShareManager::EmitApiError(QNetworkReply* reply) {
        if (!reply) {
            emit apiError("网络错误：无响应", 0);
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

    // ── Owner Operations ──

    void ShareManager::listShares(int page, int pageSize, const QString& status) {
        QUrlQuery query;
        query.addQueryItem("page", QString::number(page));
        query.addQueryItem("page_size", QString::number(pageSize));
        if (!status.isEmpty() && status != "all") {
            query.addQueryItem("status", status);
        }

        QUrl url("/api/share");
        url.setQuery(query);

        auto headers = PrepareOwnerHeaders();
        auto* reply = m_networkClient->Get(url, headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleListResponse(reply);
        });
    }

    void ShareManager::createShare(
        const QStringList& fileIds,
        const QString& permission,
        const QString& password,
        int expireDays
    ) {
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
        body["permission"] = permission;
        body["expire_days"] = expireDays;
        if (!password.isEmpty()) {
            body["password"] = password;
        }

        QJsonDocument doc(body);
        auto headers = PrepareOwnerHeaders();
        headers["Content-Type"] = "application/json";

        auto* reply = m_networkClient->Post(QUrl("/api/share"), doc.toJson(QJsonDocument::Compact), headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleCreateResponse(reply);
        });
    }

    void ShareManager::updateShare(
        const QString& shareId,
        const QString& permission,
        const QString& password,
        int expireDays
    ) {
        QJsonObject body;
        if (!permission.isEmpty()) {
            body["permission"] = permission;
        }
        if (!password.isEmpty()) {
            body["password"] = password;
        }
        if (expireDays >= 0) {
            body["expire_days"] = expireDays;
        }

        QJsonDocument doc(body);
        auto headers = PrepareOwnerHeaders();
        headers["Content-Type"] = "application/json";

        QUrl url(QString("/api/share/%1").arg(shareId));
        auto* reply = m_networkClient->Put(url, doc.toJson(QJsonDocument::Compact), headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleUpdateResponse(reply);
        });
    }

    void ShareManager::cancelShares(const QStringList& shareIds) {
        QJsonObject body;
        QJsonArray ids;
        for (const auto& id : shareIds) {
            ids.append(id);
        }
        body["share_ids"] = ids;

        QByteArray json_body = QJsonDocument(body).toJson(QJsonDocument::Compact);

        auto headers = PrepareOwnerHeaders();
        auto* reply = m_networkClient->Delete(QUrl("/api/share"), json_body, headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleCancelResponse(reply);
        });
    }

    // ── Visitor Operations ──

    void ShareManager::browseShare(const QString& shareId, const QString& parentId) {
        QUrlQuery query;
        if (!parentId.isEmpty()) {
            query.addQueryItem("folder_id", parentId);
        }

        QUrl url(QString("/api/share/browse/%1").arg(shareId));
        url.setQuery(query);

        auto headers = PrepareVisitorHeaders();
        auto* reply = m_networkClient->Get(url, headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply, shareId]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleBrowseResponse(reply, shareId);
        });
    }

    QString ShareManager::parseShareInput(const QString& input) const {
        return disk::desktop::utils::ShareCodeParser::ParseShareInput(input);
    }

    // ── Response Handlers ──

    void ShareManager::HandleListResponse(QNetworkReply* reply) {
        if (!reply) {
            emit apiError("网络错误：无响应", 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            emit apiError("响应格式无效", 0);
            return;
        }

        auto data = json_opt->value("data").toObject();
        auto items_array = data.value("items").toArray();

        QVector<disk::desktop::ShareItem> items;
        items.reserve(items_array.size());
        for (const auto& val : items_array) {
            items.append(disk::desktop::ShareItem::FromJson(val.toObject()));
        }

        m_listModel->SetItems(items);

        auto pagination = data.value("pagination").toObject();
        int page = pagination.value("page").toInt(1);
        int total_pages = pagination.value("total_pages").toInt(1);
        int total = pagination.value("total").toInt(0);
        emit paginationLoaded(page, total_pages, total);

        if (items.isEmpty()) {
            emit operationSuccess("未找到分享");
        }
    }

    void ShareManager::HandleCreateResponse(QNetworkReply* reply) {
        if (!reply) {
            emit apiError("网络错误：无响应", 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            emit apiError("响应格式无效", 0);
            return;
        }

        auto data = json_opt->value("data").toObject();
        QString share_id = data.value("share_id").toString();
        QString share_link = data.value("share_link").toString();

        emit shareCreated(share_id, share_link);
        emit operationSuccess("分享已创建");
    }

    void ShareManager::HandleDetailResponse(QNetworkReply* reply) {
        if (!reply) {
            emit apiError("网络错误：无响应", 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            emit apiError("响应格式无效", 0);
            return;
        }

        auto data = json_opt->value("data").toObject();
        QVariantMap detail;

        detail["share_id"] = data.value("share_id").toString();
        detail["share_link"] = data.value("share_link").toString();
        detail["permission"] = data.value("permission").toString();
        detail["has_password"] = data.value("has_password").toBool(false);
        detail["status"] = data.value("status").toString();
        detail["view_count"] = data.value("view_count").toInt(0);
        detail["download_count"] = data.value("download_count").toInt(0);

        if (data.contains("expires_at") && data["expires_at"].isString()) {
            detail["expires_at"] = data.value("expires_at").toString();
        }
        if (data.contains("created_at") && data["created_at"].isString()) {
            detail["created_at"] = data.value("created_at").toString();
        }

        auto files = data.value("files").toArray();
        QVariantList file_list;
        for (const auto& val : files) {
            auto obj = val.toObject();
            QVariantMap file;
            file["id"] = static_cast<double>(obj.value("id").toDouble(0));
            file["name"] = obj.value("name").toString();
            file["type"] = obj.value("type").toString();
            file["size"] = static_cast<double>(obj.value("size").toDouble(0));
            file_list.append(file);
        }
        detail["files"] = file_list;

        emit shareDetailLoaded(detail);
    }

    void ShareManager::HandleUpdateResponse(QNetworkReply* reply) {
        if (!reply) {
            emit apiError("网络错误：无响应", 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        emit operationSuccess("分享已更新");
    }

    void ShareManager::HandleCancelResponse(QNetworkReply* reply) {
        if (!reply) {
            emit apiError("网络错误：无响应", 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            emit apiError("响应格式无效", 0);
            return;
        }

        auto data = json_opt->value("data").toObject();
        auto result = disk::desktop::BatchActionResult::FromJson(data, "share_cancel");
        m_batchResultModel->SetResult(result);
        emit batchResultReady();

        if (result.failure_count == 0) {
            emit operationSuccess("所有分享已取消");
        } else if (result.success_count > 0) {
            emit operationSuccess(
                QString("已取消 %2 个分享中的 %1 个").arg(result.success_count).arg(result.total_count)
            );
        }
    }

    void ShareManager::HandleBrowseResponse(QNetworkReply* reply, const QString& shareId) {
        if (!reply) {
            emit apiError("网络错误：无响应", 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            emit apiError("响应格式无效", 0);
            return;
        }

        auto data = json_opt->value("data").toObject();
        auto items_array = data.value("items").toArray();

        QVector<disk::desktop::DriveItem> items;
        items.reserve(items_array.size());
        for (const auto& val : items_array) {
            items.append(disk::desktop::DriveItem::FromJson(val.toObject(), "share_browse"));
        }

        m_browseModel->SetItems(items);
        emit browseLoaded(shareId);

        if (items.isEmpty()) {
            emit operationSuccess("此文件夹为空");
        }
    }

    void ShareManager::HandleDetailVisitorResponse(QNetworkReply* reply) {
        HandleDetailResponse(reply);
    }

} // namespace disk::desktop::managers
