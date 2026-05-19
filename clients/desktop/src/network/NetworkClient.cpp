/**
 * @file NetworkClient.cpp
 * @brief Unified API client implementation
 *
 * @copyright Copyright (c) 2026
 */

#include "network/NetworkClient.hpp"

#include <QBuffer>
#include <QByteArray>
#include <QJsonDocument>
#include <QNetworkRequest>

namespace disk::desktop {

    namespace {

        constexpr auto DEFAULT_BASE_URL = "http://127.0.0.1:8080/";
        constexpr auto DELETE_TRANSFER_TIMEOUT_MS = 15000;

    } // namespace

    NetworkClient::NetworkClient(QObject* parent)
        : QObject(parent), m_owned_nam(this), m_nam(&m_owned_nam) {
        SetBaseUrl(DEFAULT_BASE_URL);
    }

    NetworkClient::NetworkClient(
        QNetworkAccessManager* network_access_manager,
        QObject* parent
    )
        : QObject(parent), m_owned_nam(this), m_nam(network_access_manager ? network_access_manager : &m_owned_nam) {
        SetBaseUrl(DEFAULT_BASE_URL);
    }

    void NetworkClient::SetBaseUrl(const QString& baseUrl) {
        m_base_url = baseUrl;
        if (!m_base_url.endsWith('/')) {
            m_base_url += '/';
        }
    }

    auto NetworkClient::Get(const QUrl& url, const QMap<QString, QString>& headers)
        -> QNetworkReply* {
        QNetworkRequest request = BuildRequest(url, headers);
        return m_nam->get(request);
    }

    auto NetworkClient::Post(
        const QUrl& url,
        const QByteArray& body,
        const QMap<QString, QString>& headers
    ) -> QNetworkReply* {
        QNetworkRequest request = BuildRequest(url, headers);
        if (!headers.contains("Content-Type")) {
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        }
        return m_nam->post(request, body);
    }

    auto NetworkClient::Put(
        const QUrl& url,
        const QByteArray& body,
        const QMap<QString, QString>& headers
    ) -> QNetworkReply* {
        QNetworkRequest request = BuildRequest(url, headers);
        if (!headers.contains("Content-Type")) {
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        }
        return m_nam->put(request, body);
    }

    auto NetworkClient::Patch(
        const QUrl& url,
        const QByteArray& body,
        const QMap<QString, QString>& headers
    ) -> QNetworkReply* {
        QNetworkRequest request = BuildRequest(url, headers);
        if (!headers.contains("Content-Type")) {
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        }
        return m_nam->sendCustomRequest(request, "PATCH", body);
    }

auto NetworkClient::Delete(
        const QUrl& url,
        const QMap<QString, QString>& headers
    ) -> QNetworkReply* {
        QNetworkRequest request = BuildRequest(url, headers);
        request.setTransferTimeout(DELETE_TRANSFER_TIMEOUT_MS);
        return m_nam->deleteResource(request);
    }

    auto NetworkClient::Delete(
        const QUrl& url,
        const QByteArray& body,
        const QMap<QString, QString>& headers
    ) -> QNetworkReply* {
        QNetworkRequest request = BuildRequest(url, headers);
        if (!headers.contains("Content-Type")) {
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        }
        request.setHeader(QNetworkRequest::ContentLengthHeader, body.size());
        request.setTransferTimeout(DELETE_TRANSFER_TIMEOUT_MS);
        auto* upload = new QBuffer;
        upload->setData(body);
        upload->open(QIODevice::ReadOnly);

        auto* reply = m_nam->sendCustomRequest(request, "DELETE", upload);
        upload->setParent(reply);
        return reply;
    }

    auto NetworkClient::BuildRequest(
        const QUrl& url,
        const QMap<QString, QString>& headers
    ) const -> QNetworkRequest {
        QUrl full_url;
        if (url.scheme().isEmpty()) {
            // Relative path: prepend base URL
            QString path = url.toString();
            if (path.startsWith('/')) {
                path = path.mid(1);
            }
            full_url = QUrl(m_base_url + path);
        } else {
            full_url = url;
        }

        QNetworkRequest request(full_url);
        request.setHeader(QNetworkRequest::UserAgentHeader, "DiskDesktop/1.0");

        for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
            request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
        }

        return request;
    }

} // namespace disk::desktop
