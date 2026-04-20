/**
 * @file AuthService.cpp
 * @brief Auth API call implementations
 *
 * @copyright Copyright (c) 2026
 */

#include "auth/AuthService.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>

#include "network/ErrorAdapter.hpp"
#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

namespace disk::desktop {

    AuthService::AuthService(
        NetworkClient* network_client,
        RequestFactory* request_factory,
        QObject* parent
    )
        : QObject(parent), m_network_client(network_client), m_request_factory(request_factory) {}

    void AuthService::Login(const QString& username, const QString& password) {
        QJsonObject body;
        body["username"] = username;
        body["password"] = password;

        auto* reply = m_network_client->Post(
            QUrl("api/auth/login"),
            QJsonDocument(body).toJson(QJsonDocument::Compact),
            {}
        );

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            ParseAuthResponse(reply);
            reply->deleteLater();
        });
    }

    void AuthService::Register(
        const QString& username,
        const QString& email,
        const QString& password
    ) {
        QJsonObject body;
        body["username"] = username;
        body["email"] = email;
        body["password"] = password;

        auto* reply = m_network_client->Post(
            QUrl("api/auth/register"),
            QJsonDocument(body).toJson(QJsonDocument::Compact),
            {}
        );

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            ParseAuthResponse(reply);
            reply->deleteLater();
        });
    }

    void AuthService::RefreshToken(const QString& refresh_token) {
        QJsonObject body;
        body["refresh_token"] = refresh_token;

        auto* reply = m_network_client->Post(
            QUrl("api/auth/refresh"),
            QJsonDocument(body).toJson(QJsonDocument::Compact),
            {}
        );

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            ParseRefreshResponse(reply);
            reply->deleteLater();
        });
    }

    void AuthService::Logout(const QString& access_token) {
        QMap<QString, QString> headers;
        headers["Authorization"] = "Bearer " + access_token;

        auto* reply = m_network_client->Post(
            QUrl("api/auth/logout"),
            QByteArray(),
            headers
        );

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            ParseLogoutResponse(reply);
            reply->deleteLater();
        });
    }

    void AuthService::AccessShare(
        const QString& share_id,
        const QString& password
    ) {
        QJsonObject body;
        if (!password.isEmpty()) {
            body["password"] = password;
        }

        auto* reply = m_network_client->Post(
            QUrl("api/share/access/" + share_id),
            QJsonDocument(body).toJson(QJsonDocument::Compact),
            {}
        );

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            ParseShareAccessResponse(reply);
            reply->deleteLater();
        });
    }

    auto AuthService::ParseAuthResponse(QNetworkReply* reply) -> void {
        if (reply->error() != QNetworkReply::NoError) {
            auto err = ErrorAdapter::FromNetworkError(reply->error());
            emit LoginFailure(err.code, err.message);
            return;
        }

        auto doc = QJsonDocument::fromJson(reply->readAll());
        auto json = doc.object();

        if (json.contains("error")) {
            auto err_obj = json["error"].toObject();
            emit LoginFailure(
                err_obj.value("code").toInt(0),
                err_obj.value("message").toString()
            );
            return;
        }

        auto data = json["data"].toObject();
        emit LoginSuccess(
            data["access_token"].toString(),
            data["refresh_token"].toString(),
            data["expires_in"].toInt(7200),
            data["user"].toObject()
        );
    }

    auto AuthService::ParseRefreshResponse(QNetworkReply* reply) -> void {
        if (reply->error() != QNetworkReply::NoError) {
            auto err = ErrorAdapter::FromNetworkError(reply->error());
            emit RefreshFailure(err.code, err.message);
            return;
        }

        auto doc = QJsonDocument::fromJson(reply->readAll());
        auto json = doc.object();

        if (json.contains("error")) {
            auto err_obj = json["error"].toObject();
            emit RefreshFailure(
                err_obj.value("code").toInt(0),
                err_obj.value("message").toString()
            );
            return;
        }

        auto data = json["data"].toObject();
        emit RefreshSuccess(
            data["access_token"].toString(),
            data["refresh_token"].toString(),
            data["expires_in"].toInt(7200)
        );
    }

    auto AuthService::ParseLogoutResponse(QNetworkReply* reply) -> void {
        reply->readAll();
        emit LogoutSuccess();
    }

    auto AuthService::ParseShareAccessResponse(QNetworkReply* reply) -> void {
        if (reply->error() != QNetworkReply::NoError) {
            auto err = ErrorAdapter::FromNetworkError(reply->error());
            emit ShareAccessFailure(err.code, err.message);
            return;
        }

        auto doc = QJsonDocument::fromJson(reply->readAll());
        auto json = doc.object();

        if (json.contains("error")) {
            auto err_obj = json["error"].toObject();
            emit ShareAccessFailure(
                err_obj.value("code").toInt(0),
                err_obj.value("message").toString()
            );
            return;
        }

        auto data = json["data"].toObject();
        emit ShareAccessSuccess(
            data["share_token"].toString(),
            data["expires_in"].toInt(3600),
            data["permission"].toString(),
            data["files"].toObject()
        );
    }

} // namespace disk::desktop
