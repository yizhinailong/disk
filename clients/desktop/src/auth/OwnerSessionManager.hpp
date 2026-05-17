/**
 * @file OwnerSessionManager.hpp
 * @brief Owner JWT state machine with single-flight refresh
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPromise>
#include <QString>
#include <QTimer>
#include <memory>

#include <QtQml/qqmlregistration.h>

namespace disk::desktop {

    class NetworkClient;
    class RequestFactory;

    enum class OwnerSessionState {
        LoggedOut,
        Authenticating,
        Active,
        Refreshing,
        ReauthRequired,
        LogoutPending,
    };

    /**
     * @brief Owner JWT session state machine
     *
     * States: LoggedOut -> Authenticating -> Active -> Refreshing -> Active/ReauthRequired
     * LogoutPending -> LoggedOut
     *
     * Single-flight refresh: only one refresh request in-flight at a time.
     * Other 401s wait on the same QPromise/QFuture.
     */
    class OwnerSessionManager : public QObject {
        Q_OBJECT
        Q_PROPERTY(OwnerSessionState state READ GetState NOTIFY stateChanged)
        Q_PROPERTY(int role READ GetRole NOTIFY roleChanged)

    public:
        explicit OwnerSessionManager(
            NetworkClient* network_client,
            RequestFactory* request_factory,
            QObject* parent = nullptr
        );

        auto GetState() const -> OwnerSessionState;

        void SetTokens(
            const QString& access_token,
            const QString& refresh_token,
            int expires_in_seconds
        );
        void ClearSession();

        auto GetAccessToken() const -> QString;
        auto GetRefreshToken() const -> QString;
        auto GetUserId() const -> quint64;
        auto GetUsername() const -> QString;
        auto GetRole() const -> int;
        auto ShouldRetryAfterRefresh() const -> bool;

    public slots:
        void StartLogin();
        void HandleLoginSuccess(
            const QString& access_token,
            const QString& refresh_token,
            int expires_in_seconds,
            const QJsonObject& user
        );
        void HandleLoginFailure();
        void HandleTokenExpired();
        void HandleRefreshSuccess(
            const QString& access_token,
            const QString& refresh_token,
            int expires_in_seconds
        );
        void HandleRefreshFailure();
        void StartLogout();
        void CompleteLogout();

    signals:
        void stateChanged(OwnerSessionState state);
        void loginRequired();
        void refreshRequested(const QString& refresh_token);
        void logoutRequested(const QString& access_token);
        void tokensUpdated();
        void sessionCleared();
        void roleChanged();

    private:
        void SetState(OwnerSessionState new_state);
        static int DecodeRoleFromJwt(const QString& token);

        NetworkClient* m_network_client;
        RequestFactory* m_request_factory;

        OwnerSessionState m_state{ OwnerSessionState::LoggedOut };
        QString m_access_token;
        QString m_refresh_token;
        quint64 m_user_id{ 0 };
        QString m_username;
        int m_role{ 0 };
        QTimer m_token_expiry_timer;

        std::shared_ptr<QPromise<bool>> m_refresh_promise;
        int m_replay_count{ 0 };

        static constexpr int MAX_REPLAY = 1;
    };

} // namespace disk::desktop
