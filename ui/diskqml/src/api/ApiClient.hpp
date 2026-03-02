#pragma once

#include <QNetworkAccessManager>
#include <QNetworkRequestFactory>
#include <QObject>
#include <QRestAccessManager>
#include <functional>

#include "ApiReply.hpp"

namespace disk::qml::api {

    using PostJsonCallback = std::function<void(ApiReply reply)>;

    /// REST client built on Qt 6.8 QRestAccessManager + QNetworkRequestFactory.
    /// Owns a QNetworkAccessManager and wraps it with QRestAccessManager.
    class ApiClient : public QObject {
        Q_OBJECT

    public:
        explicit ApiClient(QObject* parent = nullptr);

        virtual auto SetBaseUrl(const QUrl& url) -> void;
        virtual auto SetBearerToken(const QString& token) -> void;

        virtual auto PostJson(const QString& path, const QJsonObject& body, QObject* ctx, PostJsonCallback cb) -> void;

        virtual auto PostJsonWithBearerToken(
            const QString& path,
            const QJsonObject& body,
            const QString& bearerToken,
            QObject* ctx,
            PostJsonCallback cb
        ) -> void;

        ~ApiClient() override = default;

    private:
        QNetworkAccessManager m_nam;
        QRestAccessManager m_rest;
        QNetworkRequestFactory m_factory;
    };

} // namespace disk::qml::api
