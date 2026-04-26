/**
 * @file AuthService.cpp
 * @brief Auth API call implementations
 *
 * @copyright Copyright (c) 2026
 */

#include "auth/AuthService.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>

#include "network/ErrorAdapter.hpp"
#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

namespace disk::desktop {

    namespace {

        auto MakeInvalidResponseError() -> ApiError {
            ApiError err;
            err.code = -100;
            err.family = "protocol";
            err.category = "InvalidResponse";
            err.message = "Invalid server response";
            err.retryable = false;
            err.action = "report_error";
            return err;
        }

        auto TryReadJsonObject(
            QNetworkReply* reply,
            QJsonObject& json,
            ApiError& err
        ) -> bool {
            QJsonParseError parse_error;
            auto doc = QJsonDocument::fromJson(reply->readAll(), &parse_error);
            if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
                err = MakeInvalidResponseError();
                return false;
            }

            json = doc.object();
            return true;
        }

        auto TryReadApiFailure(const QJsonObject& json, ApiError& err) -> bool {
            if (json.contains("error") && json.value("error").isObject()) {
                err = ErrorAdapter::FromJson(json.value("error").toObject());
                return true;
            }

            auto code = json.value("code").toInt(0);
            if (code != 0) {
                err = ErrorAdapter::FromJson(json);
                return true;
            }

            return false;
        }

        auto TryReadBackendFailure(QNetworkReply* reply, ApiError& err) -> bool {
            QJsonObject json;
            ApiError parse_err;
            if (!TryReadJsonObject(reply, json, parse_err)) {
                return false;
            }

            return TryReadApiFailure(json, err);
        }

    } // namespace

    AuthService::AuthService(
        NetworkClient* network_client,
        RequestFactory* request_factory,
        QObject* parent
    )
        : QObject(parent), m_network_client(network_client), m_request_factory(request_factory) {}

    void AuthService::Login(const QString& username, const QString& password) {
        QJsonObject body;
        body["account"] = username;
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
            ParseRegisterResponse(reply);
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
            ApiError err;
            if (!TryReadBackendFailure(reply, err)) {
                err = ErrorAdapter::FromNetworkError(reply->error());
            }
            emit loginFailure(err.code, err.message);
            return;
        }

        QJsonObject json;
        ApiError err;
        if (!TryReadJsonObject(reply, json, err)) {
            emit loginFailure(err.code, err.message);
            return;
        }

        if (TryReadApiFailure(json, err)) {
            emit loginFailure(err.code, err.message);
            return;
        }

        auto data = json.value("data").toObject();
        if (data.value("access_token").toString().isEmpty() ||
            data.value("refresh_token").toString().isEmpty() ||
            !data.value("user").isObject()) {
            err = MakeInvalidResponseError();
            emit loginFailure(err.code, err.message);
            return;
        }

        emit loginSuccess(
            data.value("access_token").toString(),
            data.value("refresh_token").toString(),
            data.value("expires_in").toInt(7200),
            data.value("user").toObject()
        );
    }

    auto AuthService::ParseRegisterResponse(QNetworkReply* reply) -> void {
        if (reply->error() != QNetworkReply::NoError) {
            ApiError err;
            if (!TryReadBackendFailure(reply, err)) {
                err = ErrorAdapter::FromNetworkError(reply->error());
            }
            emit registerFailure(err.code, err.message);
            return;
        }

        QJsonObject json;
        ApiError err;
        if (!TryReadJsonObject(reply, json, err)) {
            emit registerFailure(err.code, err.message);
            return;
        }

        if (TryReadApiFailure(json, err)) {
            emit registerFailure(err.code, err.message);
            return;
        }

        auto data = json.value("data").toObject();
        if (!data.value("user").isObject()) {
            err = MakeInvalidResponseError();
            emit registerFailure(err.code, err.message);
            return;
        }

        emit registerSuccess(data.value("user").toObject());
    }

    auto AuthService::ParseRefreshResponse(QNetworkReply* reply) -> void {
        if (reply->error() != QNetworkReply::NoError) {
            ApiError err;
            if (!TryReadBackendFailure(reply, err)) {
                err = ErrorAdapter::FromNetworkError(reply->error());
            }
            emit refreshFailure(err.code, err.message);
            return;
        }

        QJsonObject json;
        ApiError err;
        if (!TryReadJsonObject(reply, json, err)) {
            emit refreshFailure(err.code, err.message);
            return;
        }

        if (TryReadApiFailure(json, err)) {
            emit refreshFailure(err.code, err.message);
            return;
        }

        auto data = json.value("data").toObject();
        if (data.value("access_token").toString().isEmpty() ||
            data.value("refresh_token").toString().isEmpty()) {
            err = MakeInvalidResponseError();
            emit refreshFailure(err.code, err.message);
            return;
        }

        emit refreshSuccess(
            data.value("access_token").toString(),
            data.value("refresh_token").toString(),
            data.value("expires_in").toInt(7200)
        );
    }

    auto AuthService::ParseLogoutResponse(QNetworkReply* reply) -> void {
        if (reply->error() != QNetworkReply::NoError) {
            ApiError err;
            if (TryReadBackendFailure(reply, err)) {
                emit logoutFailure();
                return;
            }
            // Network error without backend error: clear session regardless
            emit logoutSuccess();
            return;
        }

        QJsonObject json;
        ApiError err;
        if (!TryReadJsonObject(reply, json, err)) {
            emit logoutSuccess();
            return;
        }

        if (TryReadApiFailure(json, err)) {
            emit logoutFailure();
            return;
        }

        emit logoutSuccess();
    }

    auto AuthService::ParseShareAccessResponse(QNetworkReply* reply) -> void {
        if (reply->error() != QNetworkReply::NoError) {
            ApiError err;
            if (!TryReadBackendFailure(reply, err)) {
                err = ErrorAdapter::FromNetworkError(reply->error());
            }
            emit shareAccessFailure(err.code, err.message);
            return;
        }

        QJsonObject json;
        ApiError err;
        if (!TryReadJsonObject(reply, json, err)) {
            emit shareAccessFailure(err.code, err.message);
            return;
        }

        if (TryReadApiFailure(json, err)) {
            emit shareAccessFailure(err.code, err.message);
            return;
        }

        auto data = json.value("data").toObject();
        if (data.value("share_token").toString().isEmpty()) {
            err = MakeInvalidResponseError();
            emit shareAccessFailure(err.code, err.message);
            return;
        }

        emit shareAccessSuccess(
            data.value("share_token").toString(),
            data.value("expires_in").toInt(3600),
            data.value("permission").toString(),
            data.value("files").toObject()
        );
    }

} // namespace disk::desktop
