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
#include <QNetworkRequest>
#include <QRestReply>
#include <chrono>

namespace disk::qml::api {

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

    auto ApiClient::PostJson(const QString& path, const QJsonObject& body, QObject* ctx, PostJsonCallback cb) -> void {
        auto req = m_factory.createRequest(path);

        m_rest.post(req, QJsonDocument(body), ctx, [cb = std::move(cb)](QRestReply& reply) {
            cb(reply.hasError(), reply.errorString(), reply.httpStatus(), reply.readBody());
        });
    }

    auto ApiClient::PostJsonWithBearerToken(
        const QString& path,
        const QJsonObject& body,
        const QString& bearerToken,
        QObject* ctx,
        PostJsonCallback cb
    ) -> void {
        // Copy factory to avoid mutating shared state
        QNetworkRequestFactory local = m_factory;
        local.setBearerToken(bearerToken.toUtf8());
        auto req = local.createRequest(path);

        m_rest.post(req, QJsonDocument(body), ctx, [cb = std::move(cb)](QRestReply& reply) {
            cb(reply.hasError(), reply.errorString(), reply.httpStatus(), reply.readBody());
        });
    }

} // namespace disk::qml::api
