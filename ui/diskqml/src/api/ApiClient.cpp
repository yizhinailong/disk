#include "ApiClient.hpp"

#include <QHttpHeaders>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QRestReply>
#include <chrono>

namespace disk::qml::api {

    ApiClient::ApiClient(QObject* parent)
        : QObject(parent), m_nam(this), m_rest(&m_nam, this) {
        m_factory.setTransferTimeout(std::chrono::milliseconds{10000});
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
            ApiReply r;
            r.error.hasNetworkError = reply.hasError();
            r.error.networkError   = reply.error();
            r.error.networkErrorString = reply.errorString();
            r.error.httpStatus     = reply.httpStatus();
            r.error.httpStatusSuccess = (r.error.httpStatus >= 200 && r.error.httpStatus <= 299);
            r.body = reply.readBody();
            if (!r.body.isEmpty()) {
                QJsonDocument doc = QJsonDocument::fromJson(r.body, &r.jsonParseError);
                if (r.jsonParseError.error == QJsonParseError::NoError) {
                    r.json = std::move(doc);
                }
            }
            cb(std::move(r));
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
            ApiReply r;
            r.error.hasNetworkError = reply.hasError();
            r.error.networkError   = reply.error();
            r.error.networkErrorString = reply.errorString();
            r.error.httpStatus     = reply.httpStatus();
            r.error.httpStatusSuccess = (r.error.httpStatus >= 200 && r.error.httpStatus <= 299);
            r.body = reply.readBody();
            if (!r.body.isEmpty()) {
                QJsonDocument doc = QJsonDocument::fromJson(r.body, &r.jsonParseError);
                if (r.jsonParseError.error == QJsonParseError::NoError) {
                    r.json = std::move(doc);
                }
            }
            cb(std::move(r));
        });
    }

} // namespace disk::qml::api
