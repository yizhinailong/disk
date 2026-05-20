#include "ProfileManager.hpp"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>

namespace disk::desktop::managers {

    ProfileManager::ProfileManager(
        disk::desktop::NetworkClient* networkClient,
        disk::desktop::RequestFactory* requestFactory,
        QObject* parent
    )
        : QObject(parent),
          m_networkClient(networkClient),
          m_requestFactory(requestFactory) {}

    ProfileManager::~ProfileManager() {
        for (auto* reply : m_active_replies) {
            if (reply) {
                reply->abort();
                reply->deleteLater();
            }
        }
    }

    QVariantMap ProfileManager::userProfile() const {
        return m_userProfile;
    }

    QVariantMap ProfileManager::storageStats() const {
        return m_storageStats;
    }

    auto ProfileManager::PrepareHeaders() -> QMap<QString, QString> {
        return m_requestFactory->PrepareHeaders(disk::desktop::AuthDomain::Owner);
    }

    auto ProfileManager::ParseJsonResponse(QNetworkReply* reply) -> std::optional<QJsonObject> {
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

    auto ProfileManager::BuildApiError(QNetworkReply* reply) -> disk::desktop::ApiError {
        if (!reply) {
            disk::desktop::ApiError err;
            err.code = 0;
            err.message = "网络错误：无响应";
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

    void ProfileManager::EmitApiError(QNetworkReply* reply) {
        auto err = BuildApiError(reply);
        emit apiError(err.message, err.code);
    }

    void ProfileManager::loadProfile() {
        auto headers = PrepareHeaders();
        auto* reply = m_networkClient->Get(QUrl("/api/user/profile"), headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleProfileResponse(reply);
        });
    }

    void ProfileManager::updateProfile(const QString& nickname, const QString& avatar) {
        QJsonObject body;
        if (!nickname.isEmpty()) {
            body["nickname"] = nickname;
        }
        if (!avatar.isEmpty()) {
            body["avatar"] = avatar;
        }

        QJsonDocument doc(body);
        auto headers = PrepareHeaders();
        headers["Content-Type"] = "application/json";

        auto* reply = m_networkClient->Patch(QUrl("/api/user/profile"), doc.toJson(QJsonDocument::Compact), headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply, nickname, avatar]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleUpdateProfileResponse(reply, nickname, avatar);
        });
    }

    void ProfileManager::changePassword(const QString& oldPassword, const QString& newPassword) {
        QJsonObject body;
        body["old_password"] = oldPassword;
        body["new_password"] = newPassword;

        QJsonDocument doc(body);
        auto headers = PrepareHeaders();
        headers["Content-Type"] = "application/json";

        auto* reply = m_networkClient->Put(QUrl("/api/user/password"), doc.toJson(QJsonDocument::Compact), headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandlePasswordResponse(reply);
        });
    }

    void ProfileManager::loadStorageStats() {
        auto headers = PrepareHeaders();
        auto* reply = m_networkClient->Get(QUrl("/api/user/storage"), headers);
        m_active_replies.append(reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_active_replies.removeOne(reply);
            reply->deleteLater();
            HandleStorageResponse(reply);
        });
    }

    // ── Response handlers ──

    void ProfileManager::HandleProfileResponse(QNetworkReply* reply) {
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
        auto user = data.value("user").toObject();

        m_userProfile["id"] = static_cast<double>(user.value("id").toDouble(0));
        m_userProfile["username"] = user.value("username").toString();
        m_userProfile["nickname"] = user.value("nickname").toString();
        m_userProfile["avatar"] = user.value("avatar").toString();
        m_userProfile["created_at"] = user.value("created_at").toString();
        m_userProfile["updated_at"] = user.value("updated_at").toString();

        emit userProfileChanged();
    }

    void ProfileManager::HandleUpdateProfileResponse(QNetworkReply* reply, const QString& nickname, const QString& avatar) {
        if (!reply) {
            emit apiError("网络错误：无响应", 0);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            EmitApiError(reply);
            return;
        }

        if (!nickname.isEmpty()) {
            m_userProfile["nickname"] = nickname;
        }
        if (!avatar.isEmpty()) {
            m_userProfile["avatar"] = avatar;
        }
        emit userProfileChanged();
        emit operationSuccess("资料已更新");
    }

    void ProfileManager::HandlePasswordResponse(QNetworkReply* reply) {
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
        if (json_opt->value("code").toInt(0) != 0) {
            auto err = ErrorAdapter::FromJson(*json_opt);
            emit apiError(err.message, err.code);
            return;
        }

        emit operationSuccess("密码已修改");
    }

    void ProfileManager::HandleStorageResponse(QNetworkReply* reply) {
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

        m_storageStats["used"] = static_cast<double>(data.value("used").toDouble(0));
        m_storageStats["quota"] = static_cast<double>(data.value("quota").toDouble(0));

        if (data.contains("reserved")) {
            m_storageStats["reserved"] = static_cast<double>(data.value("reserved").toDouble(0));
        }

        emit storageStatsChanged();
    }

} // namespace disk::desktop::managers
