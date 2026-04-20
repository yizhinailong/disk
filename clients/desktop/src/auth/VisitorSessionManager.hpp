/**
 * @file VisitorSessionManager.hpp
 * @brief Visitor share-token state machine
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>

namespace disk::desktop {

    class NetworkClient;
    class RequestFactory;

    enum class VisitorSessionState {
        Idle,
        Unverified,
        Verifying,
        Active,
        ReverifyRequired,
        Closed,
    };

    /**
     * @brief Visitor share-token session state machine
     *
     * States: Idle -> Unverified -> Verifying -> Active -> ReverifyRequired/Closed
     * No refresh token - re-verification uses POST /api/share/access/{share_id} again.
     * NEVER carries Bearer token; only injects X-Share-Token.
     */
    class VisitorSessionManager : public QObject {
        Q_OBJECT
        Q_PROPERTY(VisitorSessionState state READ GetState NOTIFY stateChanged)
        Q_PROPERTY(QString shareId READ GetShareId NOTIFY shareIdChanged)
        Q_PROPERTY(QString permission READ GetPermission NOTIFY sessionEstablished)

    public:
        explicit VisitorSessionManager(
            NetworkClient* network_client,
            RequestFactory* request_factory,
            QObject* parent = nullptr
        );

        auto GetState() const -> VisitorSessionState;
        auto GetShareId() const -> QString;
        auto GetShareToken() const -> QString;
        auto GetPermission() const -> QString;

    public slots:
        void OpenShare(const QString& share_id);
        void StartVerify(const QString& password = {});
        void HandleVerifySuccess(
            const QString& share_token,
            int expires_in_seconds,
            const QString& permission,
            const QJsonObject& root_files
        );
        void HandleVerifyFailure(int error_code);
        void HandleTokenExpired();
        void CloseShare();

    signals:
        void stateChanged(VisitorSessionState state);
        void shareIdChanged(const QString& share_id);
        void sessionEstablished(const QString& permission);
        void verifyRequested(const QString& share_id, const QString& password);
        void reverifyRequested(const QString& share_id, const QString& password);
        void shareClosed();
        void shareItemsReady(const QJsonObject& root_files);

    private:
        void SetState(VisitorSessionState new_state);

        NetworkClient* m_network_client;
        RequestFactory* m_request_factory;

        VisitorSessionState m_state{ VisitorSessionState::Idle };
        QString m_share_id;
        QString m_share_token;
        QString m_permission;
        QString m_password;
        QTimer m_token_expiry_timer;
    };

} // namespace disk::desktop
