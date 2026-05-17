#include "AdminManager.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>

namespace disk::desktop {

    AdminManager::AdminManager(
        NetworkClient* network_client,
        RequestFactory* request_factory,
        QObject* parent
    )
        : QObject(parent),
          m_user_model(new AdminUserListModel(this)),
          m_share_model(new AdminShareListModel(this)),
          m_network_client(network_client),
          m_request_factory(request_factory) {}

    AdminManager::~AdminManager() {
        for (auto* reply : m_active_replies) {
            if (reply) {
                reply->abort();
                reply->deleteLater();
            }
        }
    }

    auto AdminManager::GetUserModel() const -> AdminUserListModel* {
        return m_user_model;
    }

    auto AdminManager::GetShareModel() const -> AdminShareListModel* {
        return m_share_model;
    }

    auto AdminManager::GetOverviewStats() const -> QVariantMap {
        return m_overview_stats;
    }

    auto AdminManager::GetSystemStatus() const -> QVariantMap {
        return m_system_status;
    }

    auto AdminManager::PrepareHeaders() -> QMap<QString, QString> {
        return m_request_factory->PrepareHeaders(AuthDomain::Owner);
    }

    auto AdminManager::ParseJsonResponse(QNetworkReply* reply) -> std::optional<QJsonObject> {
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

    auto AdminManager::BuildApiError(QNetworkReply* reply) -> ApiError {
        if (!reply) {
            ApiError err;
            err.code = 0;
            err.message = QStringLiteral("网络错误：无响应");
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

    void AdminManager::EmitApiError(QNetworkReply* reply) {
        auto err = BuildApiError(reply);
        emit apiError(err.message, err.code);
    }

    // ── User Management ──

    void AdminManager::ListUsers(
        int page,
        int pageSize,
        const QString& username,
        const QString& email,
        int status,
        int role
    ) {
        QUrlQuery query;
        query.addQueryItem("page", QString::number(page));
        query.addQueryItem("page_size", QString::number(pageSize));
        if (!username.isEmpty()) {
            query.addQueryItem("username", username);
        }
        if (!email.isEmpty()) {
            query.addQueryItem("email", email);
        }
        if (status >= 0) {
            query.addQueryItem("status", QString::number(status));
        }
        if (role >= 0) {
            query.addQueryItem("role", QString::number(role));
        }

        QUrl url("/api/admin/users");
        url.setQuery(query);

        auto headers = PrepareHeaders();
        auto* reply = m_network_client->Get(url, headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleListUsersResponse(reply);
        });
    }

    void AdminManager::GetUserDetail(int userId) {
        QUrl url(QString("/api/admin/users/%1").arg(userId));
        auto headers = PrepareHeaders();
        auto* reply = m_network_client->Get(url, headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleGetUserDetailResponse(reply);
        });
    }

    void AdminManager::ChangeUserStatus(int userId, int status) {
        QJsonObject body;
        body["status"] = status;

        QJsonDocument doc(body);
        auto headers = PrepareHeaders();
        headers["Content-Type"] = "application/json";

        QUrl url(QString("/api/admin/users/%1/status").arg(userId));
        auto* reply = m_network_client->Put(url, doc.toJson(QJsonDocument::Compact), headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleChangeUserStatusResponse(reply);
        });
    }

    void AdminManager::ChangeUserRole(int userId, int role) {
        QJsonObject body;
        body["role"] = role;

        QJsonDocument doc(body);
        auto headers = PrepareHeaders();
        headers["Content-Type"] = "application/json";

        QUrl url(QString("/api/admin/users/%1/role").arg(userId));
        auto* reply = m_network_client->Put(url, doc.toJson(QJsonDocument::Compact), headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleChangeUserRoleResponse(reply);
        });
    }

    void AdminManager::SoftDeleteUser(int userId) {
        QUrl url(QString("/api/admin/users/%1").arg(userId));
        auto headers = PrepareHeaders();
        auto* reply = m_network_client->Delete(url, headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleSoftDeleteUserResponse(reply);
        });
    }

    void AdminManager::ListUserFiles(int userId, int folderId, int page, int pageSize) {
        QUrlQuery query;
        query.addQueryItem("folder_id", QString::number(folderId));
        query.addQueryItem("page", QString::number(page));
        query.addQueryItem("page_size", QString::number(pageSize));

        QUrl url(QString("/api/admin/users/%1/files").arg(userId));
        url.setQuery(query);

        auto headers = PrepareHeaders();
        auto* reply = m_network_client->Get(url, headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleListUserFilesResponse(reply);
        });
    }

    void AdminManager::GetUserStorage(int userId) {
        QUrl url(QString("/api/admin/users/%1/storage").arg(userId));
        auto headers = PrepareHeaders();
        auto* reply = m_network_client->Get(url, headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleGetUserStorageResponse(reply);
        });
    }

    void AdminManager::GetGlobalStorageStats() {
        QUrl url("/api/admin/storage/stats");
        auto headers = PrepareHeaders();
        auto* reply = m_network_client->Get(url, headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleGetGlobalStorageResponse(reply);
        });
    }

    // ── Share Management ──

    void AdminManager::ListShares(int page, int pageSize, int status, int userId) {
        QUrlQuery query;
        query.addQueryItem("page", QString::number(page));
        query.addQueryItem("page_size", QString::number(pageSize));
        if (status >= 0) {
            query.addQueryItem("status", QString::number(status));
        }
        if (userId >= 0) {
            query.addQueryItem("user_id", QString::number(userId));
        }

        QUrl url("/api/admin/shares");
        url.setQuery(query);

        auto headers = PrepareHeaders();
        auto* reply = m_network_client->Get(url, headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleListSharesResponse(reply);
        });
    }

    void AdminManager::GetShareDetail(int shareId) {
        QUrl url(QString("/api/admin/shares/%1").arg(shareId));
        auto headers = PrepareHeaders();
        auto* reply = m_network_client->Get(url, headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleGetShareDetailResponse(reply);
        });
    }

    void AdminManager::ForceCancelShare(int shareId) {
        QUrl url(QString("/api/admin/shares/%1").arg(shareId));
        auto headers = PrepareHeaders();
        auto* reply = m_network_client->Delete(url, headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleForceCancelShareResponse(reply);
        });
    }

    // ── System Monitoring ──

    void AdminManager::GetOverviewStatsApi() {
        QUrl url("/api/admin/stats/overview");
        auto headers = PrepareHeaders();
        auto* reply = m_network_client->Get(url, headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleGetOverviewStatsResponse(reply);
        });
    }

    void AdminManager::GetSystemStatusApi() {
        QUrl url("/api/admin/stats/system");
        auto headers = PrepareHeaders();
        auto* reply = m_network_client->Get(url, headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleGetSystemStatusResponse(reply);
        });
    }

    // ── Response Handlers ──

    void AdminManager::HandleListUsersResponse(QNetworkReply* reply) {
        if (!reply) {
            emit apiError(QStringLiteral("网络错误：无响应"), 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            emit apiError(QStringLiteral("响应格式无效"), 0);
            return;
        }

        if (json_opt->value("code").toInt(0) != 0) {
            auto err = ErrorAdapter::FromJson(*json_opt);
            emit apiError(err.message, err.code);
            return;
        }

        auto data = json_opt->value("data").toObject();
        auto items_array = data.value("items").toArray();

        QVector<AdminUserItem> items;
        items.reserve(items_array.size());
        for (const auto& val : items_array) {
            items.append(AdminUserItem::FromJson(val.toObject()));
        }

        m_user_model->SetItems(items);

        auto pagination = data.value("pagination").toObject();
        int page = pagination.value("page").toInt(1);
        int total_pages = pagination.value("total_pages").toInt(1);
        int total = pagination.value("total").toInt(0);
        emit userPaginationLoaded(page, total_pages, total);
    }

    void AdminManager::HandleGetUserDetailResponse(QNetworkReply* reply) {
        if (!reply) {
            emit apiError(QStringLiteral("网络错误：无响应"), 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            emit apiError(QStringLiteral("响应格式无效"), 0);
            return;
        }

        if (json_opt->value("code").toInt(0) != 0) {
            auto err = ErrorAdapter::FromJson(*json_opt);
            emit apiError(err.message, err.code);
            return;
        }

        auto data = json_opt->value("data").toObject();
        QVariantMap detail;

        detail["id"] = static_cast<double>(data.value("id").toDouble(0));
        detail["username"] = data.value("username").toString();
        detail["email"] = data.value("email").toString();
        detail["nickname"] = data.value("nickname").toString();
        detail["role"] = data.value("role").toInt(0);
        detail["status"] = data.value("status").toInt(1);
        detail["storage_quota"] = static_cast<double>(data.value("storage_quota").toDouble(0));
        detail["storage_used"] = static_cast<double>(data.value("storage_used").toDouble(0));
        detail["created_at"] = data.value("created_at").toString();
        detail["last_login_at"] = data.value("last_login_at").toString();

        emit userDetailLoaded(detail);
    }

    void AdminManager::HandleChangeUserStatusResponse(QNetworkReply* reply) {
        if (!reply) {
            emit apiError(QStringLiteral("网络错误：无响应"), 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            emit apiError(QStringLiteral("响应格式无效"), 0);
            return;
        }

        if (json_opt->value("code").toInt(0) != 0) {
            auto err = ErrorAdapter::FromJson(*json_opt);
            emit apiError(err.message, err.code);
            return;
        }

        emit operationSuccess(QStringLiteral("用户状态已更新"));
    }

    void AdminManager::HandleChangeUserRoleResponse(QNetworkReply* reply) {
        if (!reply) {
            emit apiError(QStringLiteral("网络错误：无响应"), 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            emit apiError(QStringLiteral("响应格式无效"), 0);
            return;
        }

        if (json_opt->value("code").toInt(0) != 0) {
            auto err = ErrorAdapter::FromJson(*json_opt);
            emit apiError(err.message, err.code);
            return;
        }

        emit operationSuccess(QStringLiteral("用户角色已更新"));
    }

    void AdminManager::HandleSoftDeleteUserResponse(QNetworkReply* reply) {
        if (!reply) {
            emit apiError(QStringLiteral("网络错误：无响应"), 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            emit apiError(QStringLiteral("响应格式无效"), 0);
            return;
        }

        if (json_opt->value("code").toInt(0) != 0) {
            auto err = ErrorAdapter::FromJson(*json_opt);
            emit apiError(err.message, err.code);
            return;
        }

        emit operationSuccess(QStringLiteral("用户已删除"));
    }

    void AdminManager::HandleListUserFilesResponse(QNetworkReply* reply) {
        if (!reply) {
            emit apiError(QStringLiteral("网络错误：无响应"), 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            emit apiError(QStringLiteral("响应格式无效"), 0);
            return;
        }

        if (json_opt->value("code").toInt(0) != 0) {
            auto err = ErrorAdapter::FromJson(*json_opt);
            emit apiError(err.message, err.code);
            return;
        }

        auto data = json_opt->value("data").toObject();
        auto items_array = data.value("items").toArray();

        QVariantList file_list;
        for (const auto& val : items_array) {
            auto obj = val.toObject();
            QVariantMap file;
            file["id"] = static_cast<double>(obj.value("id").toDouble(0));
            file["name"] = obj.value("name").toString();
            file["type"] = obj.value("type").toString();
            file["size"] = static_cast<double>(obj.value("size").toDouble(0));
            if (obj.contains("mime_type")) {
                file["mime_type"] = obj.value("mime_type").toString();
            }
            file["parent_id"] = static_cast<double>(obj.value("parent_id").toDouble(0));
            file["created_at"] = obj.value("created_at").toString();
            file["updated_at"] = obj.value("updated_at").toString();
            file_list.append(file);
        }

        auto pagination = data.value("pagination").toObject();
        int page = pagination.value("page").toInt(1);
        int total_pages = pagination.value("total_pages").toInt(1);
        int total = pagination.value("total").toInt(0);
        emit userFilesPaginationLoaded(page, total_pages, total);
    }

    void AdminManager::HandleGetUserStorageResponse(QNetworkReply* reply) {
        if (!reply) {
            emit apiError(QStringLiteral("网络错误：无响应"), 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            emit apiError(QStringLiteral("响应格式无效"), 0);
            return;
        }

        if (json_opt->value("code").toInt(0) != 0) {
            auto err = ErrorAdapter::FromJson(*json_opt);
            emit apiError(err.message, err.code);
            return;
        }

        auto data = json_opt->value("data").toObject();
        QVariantMap storage;
        storage["total_users"] = data.value("total_users").toInt(0);
        storage["total_files"] = data.value("total_files").toInt(0);
        storage["total_storage_used"] = static_cast<double>(data.value("total_storage_used").toDouble(0));
        storage["total_storage_quota"] = static_cast<double>(data.value("total_storage_quota").toDouble(0));
        storage["active_shares"] = data.value("active_shares").toInt(0);

        emit userStorageLoaded(storage);
    }

    void AdminManager::HandleGetGlobalStorageResponse(QNetworkReply* reply) {
        if (!reply) {
            emit apiError(QStringLiteral("网络错误：无响应"), 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            emit apiError(QStringLiteral("响应格式无效"), 0);
            return;
        }

        if (json_opt->value("code").toInt(0) != 0) {
            auto err = ErrorAdapter::FromJson(*json_opt);
            emit apiError(err.message, err.code);
            return;
        }

        auto data = json_opt->value("data").toObject();
        QVariantMap storage;
        storage["total_users"] = data.value("total_users").toInt(0);
        storage["total_files"] = data.value("total_files").toInt(0);
        storage["total_storage_used"] = static_cast<double>(data.value("total_storage_used").toDouble(0));
        storage["total_storage_quota"] = static_cast<double>(data.value("total_storage_quota").toDouble(0));
        storage["active_shares"] = data.value("active_shares").toInt(0);

        emit userStorageLoaded(storage);
    }

    void AdminManager::HandleListSharesResponse(QNetworkReply* reply) {
        if (!reply) {
            emit apiError(QStringLiteral("网络错误：无响应"), 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            emit apiError(QStringLiteral("响应格式无效"), 0);
            return;
        }

        if (json_opt->value("code").toInt(0) != 0) {
            auto err = ErrorAdapter::FromJson(*json_opt);
            emit apiError(err.message, err.code);
            return;
        }

        auto data = json_opt->value("data").toObject();
        auto items_array = data.value("items").toArray();

        QVector<AdminShareItem> items;
        items.reserve(items_array.size());
        for (const auto& val : items_array) {
            items.append(AdminShareItem::FromJson(val.toObject()));
        }

        m_share_model->SetItems(items);

        auto pagination = data.value("pagination").toObject();
        int page = pagination.value("page").toInt(1);
        int total_pages = pagination.value("total_pages").toInt(1);
        int total = pagination.value("total").toInt(0);
        emit sharePaginationLoaded(page, total_pages, total);
    }

    void AdminManager::HandleGetShareDetailResponse(QNetworkReply* reply) {
        if (!reply) {
            emit apiError(QStringLiteral("网络错误：无响应"), 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            emit apiError(QStringLiteral("响应格式无效"), 0);
            return;
        }

        if (json_opt->value("code").toInt(0) != 0) {
            auto err = ErrorAdapter::FromJson(*json_opt);
            emit apiError(err.message, err.code);
            return;
        }

        auto data = json_opt->value("data").toObject();
        QVariantMap detail;

        detail["id"] = static_cast<double>(data.value("id").toDouble(0));
        detail["user_id"] = static_cast<double>(data.value("user_id").toDouble(0));
        detail["username"] = data.value("username").toString();
        detail["file_id"] = static_cast<double>(data.value("file_id").toDouble(0));
        detail["file_name"] = data.value("file_name").toString();
        detail["share_code"] = data.value("share_code").toString();
        detail["status"] = data.value("status").toInt(0);
        detail["access_count"] = data.value("access_count").toInt(0);
        detail["created_at"] = data.value("created_at").toString();
        detail["expires_at"] = data.value("expires_at").toString();

        emit shareDetailLoaded(detail);
    }

    void AdminManager::HandleForceCancelShareResponse(QNetworkReply* reply) {
        if (!reply) {
            emit apiError(QStringLiteral("网络错误：无响应"), 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            emit apiError(QStringLiteral("响应格式无效"), 0);
            return;
        }

        if (json_opt->value("code").toInt(0) != 0) {
            auto err = ErrorAdapter::FromJson(*json_opt);
            emit apiError(err.message, err.code);
            return;
        }

        emit operationSuccess(QStringLiteral("分享已强制取消"));
    }

    void AdminManager::HandleGetOverviewStatsResponse(QNetworkReply* reply) {
        if (!reply) {
            emit apiError(QStringLiteral("网络错误：无响应"), 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            emit apiError(QStringLiteral("响应格式无效"), 0);
            return;
        }

        if (json_opt->value("code").toInt(0) != 0) {
            auto err = ErrorAdapter::FromJson(*json_opt);
            emit apiError(err.message, err.code);
            return;
        }

        auto data = json_opt->value("data").toObject();
        m_overview_stats.clear();
        m_overview_stats["total_users"] = data.value("total_users").toInt(0);
        m_overview_stats["total_files"] = data.value("total_files").toInt(0);
        m_overview_stats["total_storage_used"] = static_cast<double>(data.value("total_storage_used").toDouble(0));
        m_overview_stats["total_storage_quota"] = static_cast<double>(data.value("total_storage_quota").toDouble(0));
        m_overview_stats["active_shares"] = data.value("active_shares").toInt(0);

        emit overviewStatsChanged();
    }

    void AdminManager::HandleGetSystemStatusResponse(QNetworkReply* reply) {
        if (!reply) {
            emit apiError(QStringLiteral("网络错误：无响应"), 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            emit apiError(QStringLiteral("响应格式无效"), 0);
            return;
        }

        if (json_opt->value("code").toInt(0) != 0) {
            auto err = ErrorAdapter::FromJson(*json_opt);
            emit apiError(err.message, err.code);
            return;
        }

        auto data = json_opt->value("data").toObject();
        m_system_status.clear();
        m_system_status["mysql_connected"] = data.value("mysql_connected").toBool(false);
        m_system_status["redis_connected"] = data.value("redis_connected").toBool(false);
        m_system_status["disk_total"] = static_cast<double>(data.value("disk_total").toDouble(0));
        m_system_status["disk_used"] = static_cast<double>(data.value("disk_used").toDouble(0));
        m_system_status["disk_free"] = static_cast<double>(data.value("disk_free").toDouble(0));
        m_system_status["uptime_seconds"] = data.value("uptime_seconds").toInt(0);

        emit systemStatusChanged();
    }

} // namespace disk::desktop
