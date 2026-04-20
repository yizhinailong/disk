/**
 * @file VisitorSessionManager.cpp
 * @brief Visitor share-token state machine implementation
 *
 * @copyright Copyright (c) 2026
 */

#include "auth/VisitorSessionManager.hpp"

#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

namespace disk::desktop {

    VisitorSessionManager::VisitorSessionManager(
        NetworkClient* network_client,
        RequestFactory* request_factory,
        QObject* parent
    )
        : QObject(parent), m_network_client(network_client), m_request_factory(request_factory), m_token_expiry_timer(this) {}

    auto VisitorSessionManager::GetState() const -> VisitorSessionState {
        return m_state;
    }

    auto VisitorSessionManager::GetShareId() const -> QString {
        return m_share_id;
    }

    auto VisitorSessionManager::GetShareToken() const -> QString {
        return m_share_token;
    }

    auto VisitorSessionManager::GetPermission() const -> QString {
        return m_permission;
    }

    void VisitorSessionManager::OpenShare(const QString& share_id) {
        m_share_id = share_id;
        m_password.clear();
        SetState(VisitorSessionState::Unverified);
        emit shareIdChanged(m_share_id);
    }

    void VisitorSessionManager::StartVerify(const QString& password) {
        m_password = password;
        SetState(VisitorSessionState::Verifying);
        emit verifyRequested(m_share_id, password);
    }

    void VisitorSessionManager::HandleVerifySuccess(
        const QString& share_token,
        int expires_in_seconds,
        const QString& permission,
        const QJsonObject& root_files
    ) {
        m_share_token = share_token;
        m_permission = permission;
        m_request_factory->SetVisitorShareToken(share_token);

        m_token_expiry_timer.start(
            std::max(expires_in_seconds - 60, 30) * 1000
        );

        SetState(VisitorSessionState::Active);
        emit sessionEstablished(m_permission);
        emit shareItemsReady(root_files);
    }

    void VisitorSessionManager::HandleVerifyFailure(int error_code) {
        // 60003 SharePasswordError: stay in Unverified, let user retry
        if (error_code == 60003) {
            SetState(VisitorSessionState::Unverified);
            return;
        }

        // 60001 ShareNotFound / 60002 ShareExpired: close
        if (error_code == 60001 || error_code == 60002) {
            CloseShare();
            return;
        }

        // Other errors: back to Unverified
        SetState(VisitorSessionState::Unverified);
    }

    void VisitorSessionManager::HandleTokenExpired() {
        m_share_token.clear();
        m_request_factory->ClearVisitorToken();
        m_token_expiry_timer.stop();
        SetState(VisitorSessionState::ReverifyRequired);
        emit reverifyRequested(m_share_id, m_password);
    }

    void VisitorSessionManager::CloseShare() {
        m_share_id.clear();
        m_share_token.clear();
        m_permission.clear();
        m_password.clear();
        m_request_factory->ClearVisitorToken();
        m_token_expiry_timer.stop();
        SetState(VisitorSessionState::Idle);
        emit shareClosed();
    }

    void VisitorSessionManager::SetState(VisitorSessionState new_state) {
        if (m_state != new_state) {
            m_state = new_state;
            emit stateChanged(m_state);
        }
    }

} // namespace disk::desktop
