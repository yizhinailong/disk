#include "ApiClient.hpp"

#include <QJsonDocument>
#include <QNetworkRequest>
#include <QRestReply>

namespace disk::qml::api {

    ApiClient::ApiClient(QObject* parent)
        : QObject(parent), m_nam(this), m_rest(&m_nam, this) {
    }

    auto ApiClient::SetBaseUrl(const QUrl& url) -> void {
        m_factory.setBaseUrl(url);
    }

    auto ApiClient::SetBearerToken(const QString& token) -> void {
        m_factory.setBearerToken(token.toUtf8());
    }

    auto ApiClient::PostJson(const QString& path, const QJsonObject& body, QObject* ctx, PostJsonCallback cb) -> void {
        auto req = m_factory.createRequest(path);
        req.setTransferTimeout(10000);

        m_rest.post(req, QJsonDocument(body), ctx, [cb = std::move(cb)](QRestReply& reply) {
            if (reply.isSuccess()) {
                auto json = reply.readJson();
                cb(std::move(json), QString{});
            } else {
                cb(std::nullopt, reply.errorString());
            }
        });
    }

} // namespace disk::qml::api
