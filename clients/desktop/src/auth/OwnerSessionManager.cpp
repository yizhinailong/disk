/**
 * @file OwnerSessionManager.cpp
 * @brief Owner JWT state machine implementation
 *
 * @copyright Copyright (c) 2026
 */

#include "auth/OwnerSessionManager.hpp"

#include <QByteArray>
#include <QJsonObject>

#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

namespace disk::desktop {

    OwnerSessionManager::OwnerSessionManager(
        NetworkClient* network_client,
        RequestFactory* request_factory,
        QObject* parent
    )
        : QObject(parent), m_network_client(network_client), m_request_factory(request_factory), m_token_expiry_timer(this) {
        m_token_expiry_timer.setSingleShot(true);
    }

    auto OwnerSessionManager::GetState() const -> OwnerSessionState {
        return m_state;
    }

    void OwnerSessionManager::SetTokens(
        const QString& access_token,
        const QString& refresh_token,
        int expires_in_seconds
    ) {
        m_access_token = access_token;
        m_refresh_token = refresh_token;
        m_request_factory->SetOwnerAccessToken(access_token);

        m_token_expiry_timer.start(
            std::max(expires_in_seconds - 300, 60) * 1000
        );

        emit tokensUpdated();
    }

    void OwnerSessionManager::ClearSession() {
        m_access_token.clear();
        m_refresh_token.clear();
        m_user_id = 0;
        m_username.clear();
        if (m_role != 0) {
            m_role = 0;
            emit roleChanged();
        }
        m_replay_count = 0;
        m_token_expiry_timer.stop();
        m_request_factory->ClearOwnerToken();
        m_refresh_promise.reset();

        emit sessionCleared();
    }

    auto OwnerSessionManager::GetAccessToken() const -> QString {
        return m_access_token;
    }

    auto OwnerSessionManager::GetRefreshToken() const -> QString {
        return m_refresh_token;
    }

    auto OwnerSessionManager::GetUserId() const -> quint64 {
        return m_user_id;
    }

    auto OwnerSessionManager::GetUsername() const -> QString {
        return m_username;
    }

    auto OwnerSessionManager::ShouldRetryAfterRefresh() const -> bool {
        return m_replay_count < MAX_REPLAY;
    }

    auto OwnerSessionManager::GetRole() const -> int {
        return m_role;
    }

    auto OwnerSessionManager::DecodeRoleFromJwt(const QString& token) -> int {
        if (token.isEmpty()) {
            return 0;
        }
        const auto segments = token.split(u'.');
        if (segments.size() != 3) {
            return 0;
        }
        const QByteArray payload = QByteArray::fromBase64(segments[1].toLatin1());
        const QJsonDocument doc = QJsonDocument::fromJson(payload);
        if (!doc.isObject()) {
            return 0;
        }
        return doc.object().value("role").toInt(0);
    }

    void OwnerSessionManager::StartLogin() {
        SetState(OwnerSessionState::Authenticating);
    }

    void OwnerSessionManager::HandleLoginSuccess(
        const QString& access_token,
        const QString& refresh_token,
        int expires_in_seconds,
        const QJsonObject& user
    ) {
        m_user_id = static_cast<quint64>(user.value("id").toInt(0));
        m_username = user.value("username").toString();

        const int new_role = DecodeRoleFromJwt(access_token);
        if (m_role != new_role) {
            m_role = new_role;
            emit roleChanged();
        }

        SetTokens(access_token, refresh_token, expires_in_seconds);
        SetState(OwnerSessionState::Active);
    }

    void OwnerSessionManager::HandleLoginFailure() {
        ClearSession();
        SetState(OwnerSessionState::LoggedOut);
    }

    void OwnerSessionManager::HandleTokenExpired() {
        if (m_state == OwnerSessionState::LogoutPending) {
            return;
        }

        if (m_state == OwnerSessionState::Refreshing) {
            // Already refreshing; caller should wait on the shared promise
            return;
        }

        if (m_refresh_token.isEmpty()) {
            SetState(OwnerSessionState::ReauthRequired);
            emit loginRequired();
            return;
        }

        SetState(OwnerSessionState::Refreshing);
        m_replay_count = 0;
        m_refresh_promise = std::make_shared<QPromise<bool>>();
        m_refresh_promise->start();

        emit refreshRequested(m_refresh_token);
    }

    void OwnerSessionManager::HandleRefreshSuccess(
        const QString& access_token,
        const QString& refresh_token,
        int expires_in_seconds
    ) {
        const int new_role = DecodeRoleFromJwt(access_token);
        if (m_role != new_role) {
            m_role = new_role;
            emit roleChanged();
        }

        SetTokens(access_token, refresh_token, expires_in_seconds);
        m_replay_count++;

        if (m_refresh_promise) {
            m_refresh_promise->addResult(true);
            m_refresh_promise->finish();
            m_refresh_promise.reset();
        }

        SetState(OwnerSessionState::Active);
    }

    void OwnerSessionManager::HandleRefreshFailure() {
        if (m_refresh_promise) {
            m_refresh_promise->finish();
            m_refresh_promise.reset();
        }

        ClearSession();
        SetState(OwnerSessionState::ReauthRequired);
        emit loginRequired();
    }

    void OwnerSessionManager::StartLogout() {
        SetState(OwnerSessionState::LogoutPending);

        if (m_refresh_promise) {
            m_refresh_promise->finish();
            m_refresh_promise.reset();
        }

        if (!m_access_token.isEmpty()) {
            emit logoutRequested(m_access_token);
        } else {
            CompleteLogout();
        }
    }

    void OwnerSessionManager::CompleteLogout() {
        ClearSession();
        SetState(OwnerSessionState::LoggedOut);
    }

    void OwnerSessionManager::SetState(OwnerSessionState new_state) {
        if (m_state != new_state) {
            m_state = new_state;
            emit stateChanged(m_state);
        }
    }

} // namespace disk::desktop
