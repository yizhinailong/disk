/**
 * @file ApiClient.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Qt REST client wrapper used by the QML client
 * @version 0.1
 * @date 2026-03-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkRequestFactory>
#include <QObject>
#include <QRestAccessManager>
#include <QString>
#include <functional>

namespace disk::qml::api {

    /**
     * @brief Callback invoked when a POST request completes.
     *
     * @param hasNetworkError  True if a network-level error occurred (no HTTP response received).
     * @param networkErrorString  Human-readable description of the network error; empty on success.
     * @param httpStatus  HTTP status code returned by the server (0 if hasNetworkError is true).
     * @param body  Raw response body bytes; may be empty on error.
     */
    using PostJsonCallback = std::function<void(bool hasNetworkError, QString networkErrorString, int httpStatus, QByteArray body)>;

    /**
     * @brief REST client built on Qt 6.8 QRestAccessManager + QNetworkRequestFactory.
     *
     * Owns a QNetworkAccessManager and wraps it with QRestAccessManager.
     * All requests share a common base URL and optional bearer token configured
     * via SetBaseUrl() and SetBearerToken().
     */
    class ApiClient : public QObject {
        Q_OBJECT

    public:
        explicit ApiClient(QObject* parent = nullptr);

        /**
         * @brief Set the base URL for all requests created by the internal factory.
         *
         * @param url  Base URL, e.g. "http://127.0.0.1:8080".
         */
        virtual auto SetBaseUrl(const QUrl& url) -> void;
        /**
         * @brief Set the bearer token applied to all subsequent PostJson() calls.
         *
         * @param token  Access token string (without "Bearer " prefix).
         */
        virtual auto SetBearerToken(const QString& token) -> void;

        /**
         * @brief POST a JSON body to @p path using the shared factory bearer token.
         *
         * @param path  Request path relative to the base URL (e.g. "/api/auth/login").
         * @param body  JSON object to serialize as the request body.
         * @param ctx   Context QObject whose lifetime gates the callback.
         *              The callback is NOT invoked after @p ctx is destroyed.
         *              @p ctx must remain valid until either the reply arrives or the object is deleted.
         * @param cb    Callback invoked on the Qt event loop when the reply is ready.
         */
        virtual auto PostJson(const QString& path, const QJsonObject& body, QObject* ctx, PostJsonCallback cb) -> void;

        /**
         * @brief POST a JSON body with a caller-supplied bearer token, without mutating shared state.
         *
         * Unlike PostJson(), this method creates a local copy of the internal factory and
         * sets @p bearerToken only on that copy. This avoids modifying the shared factory's
         * bearer token, which would affect concurrent or subsequent requests.
         *
         * Typical use: logout, where the access token must be passed explicitly but the shared
         * factory may already hold a different or no token.
         *
         * @param path         Request path relative to the base URL.
         * @param body         JSON object to serialize as the request body.
         * @param bearerToken  Token to use for this request only.
         * @param ctx          Context QObject; callback is suppressed after destruction.
         * @param cb           Callback invoked on the Qt event loop when the reply is ready.
         */
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
