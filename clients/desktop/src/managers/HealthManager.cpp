#include "managers/HealthManager.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include "network/NetworkClient.hpp"

namespace disk::desktop::managers {

    HealthManager::HealthManager(
        disk::desktop::NetworkClient* networkClient,
        QObject* parent
    )
        : QObject(parent), m_networkClient(networkClient) {
    }

    HealthManager::~HealthManager() {
        for (auto* reply : m_activeReplies) {
            if (reply) {
                reply->disconnect(this);
                reply->abort();
                reply->deleteLater();
            }
        }
        m_activeReplies.clear();
    }

    QVariantMap HealthManager::health() const {
        return m_health;
    }

    bool HealthManager::checking() const {
        return m_checking;
    }

    void HealthManager::checkHealth() {
        if (!m_networkClient) {
            emit apiError(QStringLiteral("网络客户端未初始化"), 0);
            return;
        }

        setChecking(true);
        auto* reply = m_networkClient->Get(QUrl("/api/health"));
        m_activeReplies.append(reply);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            m_activeReplies.removeOne(reply);
            reply->deleteLater();
            handleHealthResponse(reply);
        });
    }

    void HealthManager::setChecking(bool checking) {
        if (m_checking == checking) {
            return;
        }
        m_checking = checking;
        emit checkingChanged();
    }

    void HealthManager::handleHealthResponse(QNetworkReply* reply) {
        setChecking(false);
        if (!reply) {
            emit apiError(QStringLiteral("网络错误：无响应"), 0);
            return;
        }

        auto raw = reply->readAll();
        auto json = QJsonDocument::fromJson(raw);
        if (!json.isObject()) {
            emit apiError(reply->errorString().isEmpty() ? QStringLiteral("健康检查响应格式无效") : reply->errorString(), 0);
            return;
        }

        auto root = json.object();
        QJsonObject payload;
        if (root.value("data").isObject()) {
            payload = root.value("data").toObject();
        } else {
            payload = root;
        }

        if (!payload.contains("overall_status")) {
            emit apiError(QStringLiteral("健康检查响应缺少状态信息"), 0);
            return;
        }

        m_health = parseHealthData(payload);
        emit healthChanged();
        emit healthCheckFinished(m_health.value("overallStatus").toString());
    }

    QVariantMap HealthManager::parseHealthData(const QJsonObject& payload) {
        QVariantMap health;
        health["overallStatus"] = payload.value("overall_status").toString();
        health["version"] = payload.value("version").toString();
        health["uptime"] = payload.value("uptime").toString();
        health["timestamp"] = payload.value("timestamp").toString();
        health["totalCheckMs"] = payload.value("total_check_ms").toInt(0);

        auto components = payload.value("components").toObject();
        auto database = parseComponent(components, QStringLiteral("database"));
        auto redis = parseComponent(components, QStringLiteral("redis"));
        health["databaseStatus"] = database.value("status");
        health["databaseLatencyMs"] = database.value("latencyMs");
        health["databaseMessage"] = database.value("message");
        health["redisStatus"] = redis.value("status");
        health["redisLatencyMs"] = redis.value("latencyMs");
        health["redisMessage"] = redis.value("message");
        return health;
    }

    QVariantMap HealthManager::parseComponent(const QJsonObject& components, const QString& name) {
        auto component = components.value(name).toObject();
        QVariantMap result;
        result["status"] = component.value("status").toString();
        result["latencyMs"] = component.value("latency_ms").toInt(0);
        result["message"] = component.value("message").toString();
        return result;
    }

} // namespace disk::desktop::managers
