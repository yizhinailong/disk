/**
 * @file MockNetworkAccessManager.hpp
 * @brief Mock QNetworkAccessManager for desktop unit tests
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <QBuffer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>

namespace disk::desktop::testing {

    /**
     * @brief Pre-configured mock QNetworkReply for deterministic testing
     *
     * Returns a fixed response body and HTTP status code without network I/O.
     */
    class MockNetworkReply : public QNetworkReply {
        Q_OBJECT

    public:
        MockNetworkReply(
            const QByteArray& data,
            int http_status = 200,
            QObject* parent = nullptr
        )
            : QNetworkReply(parent), m_buffer(this), m_http_status(http_status) {
            setOpenMode(QIODevice::ReadOnly);
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, http_status);
            if (http_status >= 400) {
                setError(
                    QNetworkReply::ProtocolInvalidOperationError,
                    QString("HTTP %1").arg(http_status)
                );
            }
            m_buffer.setData(data);
            m_buffer.open(QIODevice::ReadOnly);
            setFinished(true);
            QMetaObject::invokeMethod(this, "finished", Qt::QueuedConnection);
        }

        MockNetworkReply(
            QNetworkReply::NetworkError error,
            const QString& error_string,
            QObject* parent = nullptr
        )
            : QNetworkReply(parent), m_buffer(this) {
            setError(error, error_string);
            setOpenMode(QIODevice::ReadOnly);
            setFinished(true);
            QMetaObject::invokeMethod(this, "finished", Qt::QueuedConnection);
        }

        auto readData(char* data, qint64 maxlen) -> qint64 override {
            return m_buffer.read(data, maxlen);
        }

        auto bytesAvailable() const -> qint64 override {
            return m_buffer.bytesAvailable() + QNetworkReply::bytesAvailable();
        }

        auto abort() -> void override {
            close();
        }

        void SetRawHeader(const QByteArray& name, const QByteArray& value) {
            setRawHeader(name, value);
        }

    private:
        QBuffer m_buffer;
        int m_http_status{ 200 };
    };

    /**
     * @brief Mock QNetworkAccessManager that returns pre-configured replies
     *
     * Register expected URL patterns with their JSON responses, then use
     * in place of a real QNetworkAccessManager for unit tests.
     */
    class MockNetworkAccessManager : public QNetworkAccessManager {
        Q_OBJECT

    public:
        explicit MockNetworkAccessManager(QObject* parent = nullptr)
            : QNetworkAccessManager(parent) {}

        /**
         * @brief Register a JSON response for a URL pattern
         *
         * @param url_substring Substring to match against request URL
         * @param response_json The JSON object to return
         * @param http_status HTTP status code (default 200)
         */
        void RegisterResponse(
            const QString& url_substring,
            const QJsonObject& response_json,
            int http_status = 200
        ) {
            auto data = QJsonDocument(response_json).toJson(QJsonDocument::Compact);
            m_responses.insert(url_substring, { data, http_status });
        }

        /**
         * @brief Register a raw data response for a URL pattern
         */
        void RegisterRawResponse(
            const QString& url_substring,
            const QByteArray& data,
            int http_status = 200
        ) {
            m_responses.insert(url_substring, { data, http_status });
        }

        /**
         * @brief Register a network error for a URL pattern
         */
        void RegisterError(
            const QString& url_substring,
            QNetworkReply::NetworkError error,
            const QString& error_string
        ) {
            m_errors.insert(url_substring, { error, error_string });
        }

        /**
         * @brief Clear all registered responses and errors
         */
        void Clear() {
            m_responses.clear();
            m_errors.clear();
            m_request_log.clear();
            m_request_body_log.clear();
        }

        /**
         * @brief Get the log of all requests made
         */
        auto GetRequestLog() const -> const QList<QNetworkRequest>& {
            return m_request_log;
        }

        auto GetRequestBodyLog() const -> const QList<QByteArray>& {
            return m_request_body_log;
        }

    protected:
        auto createRequest(
            Operation op,
            const QNetworkRequest& request,
            QIODevice* outgoingData = nullptr
        ) -> QNetworkReply* override {
            m_request_log.append(request);
            m_request_body_log.append(outgoingData ? outgoingData->readAll() : QByteArray());

            auto url = request.url().toString();

            // Check for registered errors first
            for (auto it = m_errors.constBegin(); it != m_errors.constEnd(); ++it) {
                if (url.contains(it.key())) {
                    return new MockNetworkReply(it.value().error, it.value().error_string, this);
                }
            }

            // Check for registered responses
            for (auto it = m_responses.constBegin(); it != m_responses.constEnd(); ++it) {
                if (url.contains(it.key())) {
                    return new MockNetworkReply(it.value().data, it.value().http_status, this);
                }
            }

            // Default: return empty 404
            return new MockNetworkReply("{}", 404, this);
        }

    private:
        struct MockResponse {
            QByteArray data;
            int http_status;
        };

        struct MockError {
            QNetworkReply::NetworkError error;
            QString error_string;
        };

        QMap<QString, MockResponse> m_responses;
        QMap<QString, MockError> m_errors;
        QList<QNetworkRequest> m_request_log;
        QList<QByteArray> m_request_body_log;
    };

} // namespace disk::desktop::testing
