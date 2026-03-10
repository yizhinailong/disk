/**
 * @file TokenRefreshCoordinator.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief TokenRefreshCoordinator implementation
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "TokenRefreshCoordinator.hpp"

#include <QDateTime>
#include <QMutexLocker>

#include <api/ApiClient.hpp>
#include <services/AuthService.hpp>
#include <services/TokenStore.hpp>
#include <utils/ErrorCode.hpp>

namespace disk::qml::services {

    TokenRefreshCoordinator::TokenRefreshCoordinator(
        AuthService* authService,
        TokenStore* tokenStore,
        api::ApiClient* apiClient,
        QObject* parent
    ) : QObject(parent),
        m_auth_service(authService),
        m_token_store(tokenStore),
        m_api_client(apiClient) {
        StartProactiveTimer();
    }

    auto TokenRefreshCoordinator::IsTokenExpired(const models::ApiEnvelope& envelope) -> bool {
        return envelope.code == static_cast<int>(utils::ErrorCode::TokenExpired);
    }

    auto TokenRefreshCoordinator::RequestRefresh(RefreshDoneCallback cb) -> void {
        QMutexLocker lock(&m_mutex);

        m_waiters.push_back(std::move(cb));

        if (m_refresh_in_flight) {
            return;
        }

        m_refresh_in_flight = true;
        lock.unlock();

        DoRefresh();
    }

    auto TokenRefreshCoordinator::HandleIfTokenExpired(
        const models::ApiEnvelope& envelope,
        RefreshDoneCallback cb
    ) -> bool {
        if (!IsTokenExpired(envelope)) {
            return false;
        }
        RequestRefresh(std::move(cb));
        return true;
    }

    auto TokenRefreshCoordinator::OnProactiveTimerTick() -> void {
        {
            QMutexLocker lock(&m_mutex);
            if (m_refresh_in_flight) {
                return;
            }
        }

        const QDateTime expiresAt = m_token_store->ExpiresAt();
        if (!expiresAt.isValid()) {
            return;
        }

        const QDateTime now = QDateTime::currentDateTimeUtc();
        const qint64 secsRemaining = now.secsTo(expiresAt);

        if (secsRemaining > kProactiveRefreshWindowSecs || secsRemaining <= 0) {
            return;
        }

        // Token expires within the proactive window — refresh silently
        RequestRefresh([](bool /*success*/) {
            // Proactive refresh: no caller to notify beyond the waiter mechanism
        });
    }

    auto TokenRefreshCoordinator::StartProactiveTimer() -> void {
        m_proactive_timer.setInterval(kTimerIntervalMs);
        connect(&m_proactive_timer, &QTimer::timeout, this, &TokenRefreshCoordinator::OnProactiveTimerTick);
        m_proactive_timer.start();
    }

    auto TokenRefreshCoordinator::DoRefresh() -> void {
        const QString refreshToken = m_token_store->RefreshToken();

        if (refreshToken.isEmpty()) {
            OnRefreshFailed();
            return;
        }

        auto* ctx = new QObject(this);

        m_auth_service->Refresh(
            refreshToken,
            ctx,
            [this, ctx](std::optional<models::RefreshResultDto> result, QString /*errorMessage*/) {
                ctx->deleteLater();

                if (!result) {
                    OnRefreshFailed();
                    return;
                }

                // Update the shared ApiClient bearer token for subsequent requests
                m_api_client->SetBearerToken(result->accessToken);

                ResolveWaiters(true);
            }
        );
    }

    auto TokenRefreshCoordinator::ResolveWaiters(bool success) -> void {
        std::vector<RefreshDoneCallback> waiters;
        {
            QMutexLocker lock(&m_mutex);
            m_refresh_in_flight = false;
            waiters.swap(m_waiters);
        }

        for (auto& waiter : waiters) {
            waiter(success);
        }
    }

    auto TokenRefreshCoordinator::OnRefreshFailed() -> void {
        m_token_store->Clear();
        m_api_client->SetBearerToken(QString{});

        ResolveWaiters(false);

        emit forceLogout();
    }

} // namespace disk::qml::services
