#pragma once

#include <QNetworkReply>
#include <QObject>
#include <QVariantMap>

namespace disk::desktop {
    class NetworkClient;
}

namespace disk::desktop::managers {

    class HealthManager : public QObject {
        Q_OBJECT
        Q_PROPERTY(QVariantMap health READ health NOTIFY healthChanged)
        Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)

    public:
        explicit HealthManager(
            disk::desktop::NetworkClient* networkClient,
            QObject* parent = nullptr
        );
        ~HealthManager() override;

        [[nodiscard]] QVariantMap health() const;
        [[nodiscard]] bool checking() const;

        Q_INVOKABLE void checkHealth();

    signals:
        void healthChanged();
        void checkingChanged();
        void healthCheckFinished(const QString& overallStatus);
        void apiError(const QString& message, int code);

    private:
        void setChecking(bool checking);
        void handleHealthResponse(QNetworkReply* reply);
        static QVariantMap parseHealthData(const QJsonObject& payload);
        static QVariantMap parseComponent(const QJsonObject& components, const QString& name);

        disk::desktop::NetworkClient* m_networkClient;
        QVariantMap m_health;
        bool m_checking{ false };
        QVector<QNetworkReply*> m_activeReplies;
    };

} // namespace disk::desktop::managers
