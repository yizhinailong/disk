/**
 * @file ApiClient.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief ApiClient implementation
 * @version 0.1
 * @date 2026-03-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "ApiClient.hpp"

#include <QHttpHeaders>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRestReply>
#include <chrono>

namespace disk::qml::api {

    // ==================== Helpers ====================

    namespace {

        /// Common reply handler shared by all request methods.
        auto MakeReplyHandler(ApiReplyCallback cb) {
            return [cb = std::move(cb)](QRestReply& reply) {
                cb(reply.hasError(), reply.errorString(), reply.httpStatus(), reply.readBody());
            };
        }

    } // anonymous namespace

    // ==================== Ctor / Config ====================

    ApiClient::ApiClient(QObject* parent)
        : QObject(parent), m_nam(this), m_rest(&m_nam, this) {
        m_factory.setTransferTimeout(std::chrono::milliseconds{ 10000 });
        m_factory.commonHeaders().append(QHttpHeaders::WellKnownHeader::Accept, "application/json");
    }

    auto ApiClient::SetBaseUrl(const QUrl& url) -> void {
        m_factory.setBaseUrl(url);
    }

    auto ApiClient::SetBearerToken(const QString& token) -> void {
        m_factory.setBearerToken(token.toUtf8());
    }

    // ==================== POST ====================

    auto ApiClient::PostJson(const QString& path, const QJsonObject& body, QObject* ctx, ApiReplyCallback cb) -> void {
        auto req = m_factory.createRequest(path);
        m_rest.post(req, QJsonDocument(body), ctx, MakeReplyHandler(std::move(cb)));
    }

    auto ApiClient::PostJsonWithBearerToken(
        const QString& path,
        const QJsonObject& body,
        const QString& bearerToken,
        QObject* ctx,
        ApiReplyCallback cb
    ) -> void {
        QNetworkRequestFactory local = m_factory;
        local.setBearerToken(bearerToken.toUtf8());
        auto req = local.createRequest(path);
        m_rest.post(req, QJsonDocument(body), ctx, MakeReplyHandler(std::move(cb)));
    }

    // ==================== POST Raw Bytes ====================

    auto ApiClient::PostRaw(const QString& path, const QUrlQuery& query, const QByteArray& body, QObject* ctx, ApiReplyCallback cb) -> void {
        auto req = m_factory.createRequest(path, query);
        req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/octet-stream"));
        m_rest.sendCustomRequest(req, "POST", body, ctx, MakeReplyHandler(std::move(cb)));
    }

    // ==================== GET ====================

    auto ApiClient::Get(const QString& path, QObject* ctx, ApiReplyCallback cb) -> void {
        auto req = m_factory.createRequest(path);
        m_rest.get(req, ctx, MakeReplyHandler(std::move(cb)));
    }

    auto ApiClient::Get(const QString& path, const QUrlQuery& query, QObject* ctx, ApiReplyCallback cb) -> void {
        auto req = m_factory.createRequest(path, query);
        m_rest.get(req, ctx, MakeReplyHandler(std::move(cb)));
    }

    auto ApiClient::GetWithBearerToken(
        const QString& path,
        const QUrlQuery& query,
        const QString& bearerToken,
        QObject* ctx,
        ApiReplyCallback cb
    ) -> void {
        QNetworkRequestFactory local = m_factory;
        local.setBearerToken(bearerToken.toUtf8());
        auto req = local.createRequest(path, query);
        m_rest.get(req, ctx, MakeReplyHandler(std::move(cb)));
    }

    // ==================== PUT ====================

    auto ApiClient::PutJson(const QString& path, const QJsonObject& body, QObject* ctx, ApiReplyCallback cb) -> void {
        auto req = m_factory.createRequest(path);
        m_rest.put(req, QJsonDocument(body), ctx, MakeReplyHandler(std::move(cb)));
    }

    auto ApiClient::PutJsonWithBearerToken(
        const QString& path,
        const QJsonObject& body,
        const QString& bearerToken,
        QObject* ctx,
        ApiReplyCallback cb
    ) -> void {
        QNetworkRequestFactory local = m_factory;
        local.setBearerToken(bearerToken.toUtf8());
        auto req = local.createRequest(path);
        m_rest.put(req, QJsonDocument(body), ctx, MakeReplyHandler(std::move(cb)));
    }

    // ==================== PATCH ====================

    auto ApiClient::PatchJson(const QString& path, const QJsonObject& body, QObject* ctx, ApiReplyCallback cb) -> void {
        auto req = m_factory.createRequest(path);
        m_rest.patch(req, QJsonDocument(body), ctx, MakeReplyHandler(std::move(cb)));
    }

    auto ApiClient::PatchJsonWithBearerToken(
        const QString& path,
        const QJsonObject& body,
        const QString& bearerToken,
        QObject* ctx,
        ApiReplyCallback cb
    ) -> void {
        QNetworkRequestFactory local = m_factory;
        local.setBearerToken(bearerToken.toUtf8());
        auto req = local.createRequest(path);
        m_rest.patch(req, QJsonDocument(body), ctx, MakeReplyHandler(std::move(cb)));
    }

    // ==================== DELETE ====================

    auto ApiClient::Delete(const QString& path, QObject* ctx, ApiReplyCallback cb) -> void {
        auto req = m_factory.createRequest(path);
        m_rest.deleteResource(req, ctx, MakeReplyHandler(std::move(cb)));
    }

    auto ApiClient::DeleteWithBearerToken(
        const QString& path,
        const QString& bearerToken,
        QObject* ctx,
        ApiReplyCallback cb
    ) -> void {
        QNetworkRequestFactory local = m_factory;
        local.setBearerToken(bearerToken.toUtf8());
        auto req = local.createRequest(path);
        m_rest.deleteResource(req, ctx, MakeReplyHandler(std::move(cb)));
    }

    auto ApiClient::DeleteJson(const QString& path, const QJsonObject& body, QObject* ctx, ApiReplyCallback cb) -> void {
        auto req = m_factory.createRequest(path);
        req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
        const QByteArray jsonBody = QJsonDocument(body).toJson(QJsonDocument::Compact);
        m_rest.sendCustomRequest(req, "DELETE", jsonBody, ctx, MakeReplyHandler(std::move(cb)));
    }

    auto ApiClient::DeleteJsonWithBearerToken(
        const QString& path,
        const QJsonObject& body,
        const QString& bearerToken,
        QObject* ctx,
        ApiReplyCallback cb
    ) -> void {
        QNetworkRequestFactory local = m_factory;
        local.setBearerToken(bearerToken.toUtf8());
        auto req = local.createRequest(path);
        req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
        const QByteArray jsonBody = QJsonDocument(body).toJson(QJsonDocument::Compact);
        m_rest.sendCustomRequest(req, "DELETE", jsonBody, ctx, MakeReplyHandler(std::move(cb)));
    }

    // ==================== Streaming ====================

    auto ApiClient::NetworkAccessManager() -> QNetworkAccessManager* {
        return &m_nam;
    }

    auto ApiClient::CreateStreamingRequest(const QString& path) -> QNetworkRequest {
        return m_factory.createRequest(path);
    }

} // namespace disk::qml::api
