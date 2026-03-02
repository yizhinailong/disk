#pragma once

#include "IApiClient.hpp"

#include <QNetworkAccessManager>
#include <QNetworkRequestFactory>
#include <QObject>
#include <QRestAccessManager>

namespace disk::qml::api {

    /// REST client built on Qt 6.8 QRestAccessManager + QNetworkRequestFactory.
    /// Owns a QNetworkAccessManager and wraps it with QRestAccessManager.
    class ApiClient final : public QObject, public IApiClient {
        Q_OBJECT

    public:
        explicit ApiClient(QObject* parent = nullptr);

        auto SetBaseUrl(const QUrl& url) -> void override;
        auto SetBearerToken(const QString& token) -> void override;

        auto PostJson(const QString& path, const QJsonObject& body, QObject* ctx, PostJsonCallback cb) -> void override;

        auto PostJsonWithBearerToken(
            const QString& path,
            const QJsonObject& body,
            const QString& bearerToken,
            QObject* ctx,
            PostJsonCallback cb
        ) -> void override;

    private:
        QNetworkAccessManager m_nam;
        QRestAccessManager m_rest;
        QNetworkRequestFactory m_factory;
    };

} // namespace disk::qml::api
