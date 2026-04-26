/**
 * @file AuthService.hpp
 * @brief Login, register, refresh, logout, share-access API calls
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

class QNetworkReply;

namespace disk::desktop {

    class NetworkClient;
    class RequestFactory;

    class AuthService : public QObject {
        Q_OBJECT

    public:
        explicit AuthService(
            NetworkClient* network_client,
            RequestFactory* request_factory,
            QObject* parent = nullptr
        );

    public slots:
        void Login(const QString& username, const QString& password);
        void Register(
            const QString& username,
            const QString& email,
            const QString& password
        );
        void RefreshToken(const QString& refresh_token);
        void Logout(const QString& access_token);
        void AccessShare(
            const QString& share_id,
            const QString& password = {}
        );

    signals:
        void loginSuccess(
            const QString& access_token,
            const QString& refresh_token,
            int expires_in,
            const QJsonObject& user
        );
        void loginFailure(int error_code, const QString& message);

        void registerSuccess(const QJsonObject& user);
        void registerFailure(int error_code, const QString& message);

        void refreshSuccess(
            const QString& access_token,
            const QString& refresh_token,
            int expires_in
        );
        void refreshFailure(int error_code, const QString& message);

        void logoutSuccess();
        void logoutFailure();

        void shareAccessSuccess(
            const QString& share_token,
            int expires_in,
            const QString& permission,
            const QJsonObject& files
        );
        void shareAccessFailure(int error_code, const QString& message);

    private:
        auto ParseAuthResponse(QNetworkReply* reply) -> void;
        auto ParseRegisterResponse(QNetworkReply* reply) -> void;
        auto ParseRefreshResponse(QNetworkReply* reply) -> void;
        auto ParseLogoutResponse(QNetworkReply* reply) -> void;
        auto ParseShareAccessResponse(QNetworkReply* reply) -> void;

        NetworkClient* m_network_client;
        RequestFactory* m_request_factory;
    };

} // namespace disk::desktop
