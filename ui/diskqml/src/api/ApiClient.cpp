/**
 * @file ApiClient.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief ApiClient 实现
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

    // ==================== 辅助函数 ====================

    namespace {

        /// 所有请求方法共享的通用响应处理器。
        auto MakeReplyHandler(ApiReplyCallback cb) {
            return [cb = std::move(cb)](QRestReply& reply) {
                cb(reply.hasError(), reply.errorString(), reply.httpStatus(), reply.readBody());
            };
        }

    } // anonymous namespace

    // ==================== 构造函数 / 配置 ====================

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

    // ==================== POST 请求 ====================

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

    // ==================== POST 原始字节 ====================

    auto ApiClient::PostRaw(const QString& path, const QUrlQuery& query, const QByteArray& body, QObject* ctx, ApiReplyCallback cb) -> void {
        auto req = m_factory.createRequest(path, query);
        req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/octet-stream"));
        m_rest.sendCustomRequest(req, "POST", body, ctx, MakeReplyHandler(std::move(cb)));
    }

    // ==================== GET 请求 ====================

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

    // ==================== PUT 请求 ====================

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

    // ==================== PATCH 请求 ====================

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

    // ==================== DELETE 请求 ====================

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

    // ==================== 流式传输 ====================

    auto ApiClient::NetworkAccessManager() -> QNetworkAccessManager* {
        return &m_nam;
    }

    auto ApiClient::CreateStreamingRequest(const QString& path) -> QNetworkRequest {
        return m_factory.createRequest(path);
    }

} // namespace disk::qml::api
