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
#include <QUrlQuery>
#include <functional>

class QJsonObject;

namespace disk::qml::api {

    /**
     * @brief Callback invoked when an HTTP request completes.
     *
     * @param hasNetworkError  True if a network-level error occurred (no HTTP response received).
     * @param networkErrorString  Human-readable description of the network error; empty on success.
     * @param httpStatus  HTTP status code returned by the server (0 if hasNetworkError is true).
     * @param body  Raw response body bytes; may be empty on error.
     */
    using ApiReplyCallback = std::function<void(bool hasNetworkError, QString networkErrorString, int httpStatus, QByteArray body)>;

    /// @brief Legacy alias kept for source compatibility.
    using PostJsonCallback = ApiReplyCallback;

    /**
     * @brief REST client built on Qt 6.8 QRestAccessManager + QNetworkRequestFactory.
     *
     * Owns a QNetworkAccessManager and wraps it with QRestAccessManager.
     * All requests share a common base URL and optional bearer token configured
     * via SetBaseUrl() and SetBearerToken().
     *
     * Every request method has a variant that accepts an explicit bearerToken
     * parameter. That token is applied to a local copy of the factory so it
     * never mutates shared state.
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
         * @brief Set the bearer token applied to all subsequent requests.
         *
         * @param token  Access token string (without "Bearer " prefix).
         */
        virtual auto SetBearerToken(const QString& token) -> void;

        // ==================== POST ====================

        /**
         * @brief POST a JSON body to @p path using the shared factory bearer token.
         */
        virtual auto PostJson(const QString& path, const QJsonObject& body, QObject* ctx, ApiReplyCallback cb) -> void;

        /**
         * @brief POST a JSON body with a caller-supplied bearer token, without mutating shared state.
         */
        virtual auto PostJsonWithBearerToken(
            const QString& path,
            const QJsonObject& body,
            const QString& bearerToken,
            QObject* ctx,
            ApiReplyCallback cb
        ) -> void;

        // ==================== GET ====================

        /**
         * @brief GET @p path (no query parameters) using the shared bearer token.
         */
        virtual auto Get(const QString& path, QObject* ctx, ApiReplyCallback cb) -> void;

        /**
         * @brief GET @p path with @p query parameters using the shared bearer token.
         *
         * @param query  URL query parameters built via QUrlQuery.
         */
        virtual auto Get(const QString& path, const QUrlQuery& query, QObject* ctx, ApiReplyCallback cb) -> void;

        /**
         * @brief GET @p path with a caller-supplied bearer token.
         */
        virtual auto GetWithBearerToken(
            const QString& path,
            const QUrlQuery& query,
            const QString& bearerToken,
            QObject* ctx,
            ApiReplyCallback cb
        ) -> void;

        // ==================== PUT ====================

        /**
         * @brief PUT a JSON body to @p path using the shared bearer token.
         */
        virtual auto PutJson(const QString& path, const QJsonObject& body, QObject* ctx, ApiReplyCallback cb) -> void;

        /**
         * @brief PUT a JSON body with a caller-supplied bearer token.
         */
        virtual auto PutJsonWithBearerToken(
            const QString& path,
            const QJsonObject& body,
            const QString& bearerToken,
            QObject* ctx,
            ApiReplyCallback cb
        ) -> void;

        // ==================== PATCH ====================

        /**
         * @brief PATCH a JSON body to @p path using the shared bearer token.
         */
        virtual auto PatchJson(const QString& path, const QJsonObject& body, QObject* ctx, ApiReplyCallback cb) -> void;

        /**
         * @brief PATCH a JSON body with a caller-supplied bearer token.
         */
        virtual auto PatchJsonWithBearerToken(
            const QString& path,
            const QJsonObject& body,
            const QString& bearerToken,
            QObject* ctx,
            ApiReplyCallback cb
        ) -> void;

        // ==================== DELETE ====================

        /**
         * @brief DELETE @p path (no body) using the shared bearer token.
         */
        virtual auto Delete(const QString& path, QObject* ctx, ApiReplyCallback cb) -> void;

        /**
         * @brief DELETE @p path (no body) with a caller-supplied bearer token.
         */
        virtual auto DeleteWithBearerToken(
            const QString& path,
            const QString& bearerToken,
            QObject* ctx,
            ApiReplyCallback cb
        ) -> void;

        /**
         * @brief DELETE with a JSON body via sendCustomRequest.
         *
         * QRestAccessManager::deleteResource() does not support request bodies.
         * This method uses sendCustomRequest("DELETE", body) to work around
         * that limitation. Required by the trash batch-delete endpoint.
         */
        virtual auto DeleteJson(const QString& path, const QJsonObject& body, QObject* ctx, ApiReplyCallback cb) -> void;

        /**
         * @brief DELETE with a JSON body and a caller-supplied bearer token.
         */
        virtual auto DeleteJsonWithBearerToken(
            const QString& path,
            const QJsonObject& body,
            const QString& bearerToken,
            QObject* ctx,
            ApiReplyCallback cb
        ) -> void;

        ~ApiClient() override = default;

    private:
        QNetworkAccessManager m_nam;
        QRestAccessManager m_rest;
        QNetworkRequestFactory m_factory;
    };

} // namespace disk::qml::api
