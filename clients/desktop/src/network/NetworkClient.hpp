/**
 * @file NetworkClient.hpp
 * @brief Unified API client wrapping QNetworkAccessManager
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QUrl>

namespace disk::desktop {

    /**
     * @brief Unified HTTP client wrapping QNetworkAccessManager
     *
     * Provides GET/POST/PUT/PATCH/DELETE methods with configurable base URL
     * and custom headers. All requests go through this single entry point.
     */
    class NetworkClient : public QObject {
        Q_OBJECT

    public:
        explicit NetworkClient(QObject* parent = nullptr);
        explicit NetworkClient(
            QNetworkAccessManager* network_access_manager,
            QObject* parent = nullptr
        );

        void SetBaseUrl(const QString& baseUrl);
        [[nodiscard]] auto GetBaseUrl() const -> const QString& { return m_base_url; }

        auto Get(const QUrl& url, const QMap<QString, QString>& headers = {})
            -> QNetworkReply*;
        auto Post(
            const QUrl& url,
            const QByteArray& body,
            const QMap<QString, QString>& headers = {}
        ) -> QNetworkReply*;
        auto Put(
            const QUrl& url,
            const QByteArray& body,
            const QMap<QString, QString>& headers = {}
        ) -> QNetworkReply*;
        auto Patch(
            const QUrl& url,
            const QByteArray& body,
            const QMap<QString, QString>& headers = {}
        ) -> QNetworkReply*;
        auto Delete(const QUrl& url, const QMap<QString, QString>& headers = {})
            -> QNetworkReply*;

    private:
        auto BuildRequest(
            const QUrl& url,
            const QMap<QString, QString>& headers
        ) const -> QNetworkRequest;

        QNetworkAccessManager m_owned_nam;
        QNetworkAccessManager* m_nam;
        QString m_base_url;
    };

} // namespace disk::desktop
