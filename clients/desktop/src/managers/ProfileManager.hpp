#pragma once

#include <QMap>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <optional>

#include "network/ErrorAdapter.hpp"
#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

namespace disk::desktop::managers {

    class ProfileManager : public QObject {
        Q_OBJECT
        Q_PROPERTY(QVariantMap userProfile READ userProfile NOTIFY userProfileChanged)
        Q_PROPERTY(QVariantMap storageStats READ storageStats NOTIFY storageStatsChanged)

    public:
        explicit ProfileManager(
            disk::desktop::NetworkClient* networkClient,
            disk::desktop::RequestFactory* requestFactory,
            QObject* parent = nullptr
        );
        ~ProfileManager() override;

        QVariantMap userProfile() const;
        QVariantMap storageStats() const;

        Q_INVOKABLE void loadProfile();
        Q_INVOKABLE void updateProfile(const QString& nickname, const QString& avatar);
        Q_INVOKABLE void changePassword(const QString& oldPassword, const QString& newPassword);
        Q_INVOKABLE void loadStorageStats();

    signals:
        void apiError(const QString& message, int code);
        void userProfileChanged();
        void storageStatsChanged();
        void operationSuccess(const QString& message);

    private:
        auto PrepareHeaders() -> QMap<QString, QString>;
        static auto ParseJsonResponse(QNetworkReply* reply) -> std::optional<QJsonObject>;
        static auto BuildApiError(QNetworkReply* reply) -> disk::desktop::ApiError;
        void HandleProfileResponse(QNetworkReply* reply);
        void HandleUpdateProfileResponse(QNetworkReply* reply, const QString& nickname, const QString& avatar);
        void HandlePasswordResponse(QNetworkReply* reply);
        void HandleStorageResponse(QNetworkReply* reply);
        void EmitApiError(QNetworkReply* reply);

        QVariantMap m_userProfile;
        QVariantMap m_storageStats;
        disk::desktop::NetworkClient* m_networkClient;
        disk::desktop::RequestFactory* m_requestFactory;
        QVector<QNetworkReply*> m_active_replies;
    };

} // namespace disk::desktop::managers
