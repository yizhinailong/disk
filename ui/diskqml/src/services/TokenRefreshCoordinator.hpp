/**
 * @file TokenRefreshCoordinator.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Single-flight token refresh coordinator with proactive refresh timer
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <QMutex>
#include <QObject>
#include <QTimer>
#include <functional>
#include <vector>

#include <dtos/ApiEnvelope.hpp>

namespace disk::qml::api {
    class ApiClient;
}

namespace disk::qml::services {

    class AuthService;
    class TokenStore;

    /**
     * @brief Coordinates token refresh across all API callers with single-flight guarantee.
     *
     * @details
     * Responsibilities:
     *   - Detects error code 40108 (TokenExpired) in API envelopes
     *   - Performs refresh via AuthService using the stored refresh token
     *   - Ensures only ONE refresh request is in flight at any time; concurrent
     *     callers wait and share the result of the in-flight refresh
     *   - Supports retry-once: after a successful refresh, the original request
     *     callback is re-invoked with `refreshed=true` so the caller can retry
     *   - Runs a proactive QTimer that refreshes the access token when it is
     *     within 5 minutes of expiry, avoiding 40108 errors in the first place
     *   - On refresh failure (40110, 40111, or network error), clears tokens and
     *     emits forceLogout() so the UI can redirect to the login screen
     *
     * Thread safety:
     *   All public methods must be called from the main (GUI) thread.
     *   The internal QMutex guards the in-flight flag and waiter list against
     *   reentrancy from nested event-loop dispatches.
     *
     * Ownership:
     *   Does not own any of its dependencies. The caller must ensure that
     *   AuthService, TokenStore, and ApiClient outlive this coordinator.
     */
    class TokenRefreshCoordinator : public QObject {
        Q_OBJECT

    public:
        /**
         * @brief Callback type for refresh waiters.
         * @param success  True if tokens were successfully refreshed.
         *
         * On success the caller should retry the original request with the new
         * access token from TokenStore. On failure the caller should propagate
         * the error to the user (a forceLogout signal will also have been emitted).
         */
        using RefreshDoneCallback = std::function<void(bool success)>;

        /**
         * @brief Construct the coordinator and start the proactive refresh timer.
         *
         * @param authService  Service used to call POST /api/auth/refresh.
         * @param tokenStore   Persistent token storage.
         * @param apiClient    Shared API client (bearer token is updated on refresh).
         * @param parent       QObject parent for lifetime management.
         */
        explicit TokenRefreshCoordinator(
            AuthService* authService,
            TokenStore* tokenStore,
            api::ApiClient* apiClient,
            QObject* parent = nullptr
        );

        /**
         * @brief Check whether an API envelope contains a TokenExpired error.
         *
         * @param envelope  The envelope from an API response.
         * @return True if envelope.code == 40108 (TokenExpired).
         */
        [[nodiscard]] static auto IsTokenExpired(const models::ApiEnvelope& envelope) -> bool;

        /**
         * @brief Request a token refresh (single-flight).
         *
         * @details
         * If no refresh is currently in flight, initiates one immediately.
         * If a refresh IS in flight, the callback is queued and will be invoked
         * when the in-flight refresh completes (with the same success/failure result).
         *
         * @param cb  Called when the refresh attempt completes.
         */
        auto RequestRefresh(RefreshDoneCallback cb) -> void;

        /**
         * @brief Convenience: check an envelope and trigger refresh if 40108.
         *
         * @details
         * If the envelope is NOT a 40108 error, returns false immediately.
         * If it IS 40108, queues a refresh and invokes @p cb when done.
         *
         * @param envelope  The API response envelope to inspect.
         * @param cb        Called when the refresh attempt completes.
         * @return True if a refresh was triggered (caller should NOT process the envelope).
         */
        auto HandleIfTokenExpired(const models::ApiEnvelope& envelope, RefreshDoneCallback cb) -> bool;

    signals:
        /**
         * @brief Emitted when token refresh fails irrecoverably.
         *
         * @details
         * Connected by SessionViewModel to trigger a full logout + redirect to login.
         * Emitted after tokens have already been cleared from TokenStore.
         */
        void forceLogout();

    private:
        // ==================== Proactive Refresh ====================

        /**
         * @brief Timer callback: check if the access token expires within 5 minutes.
         *
         * @details
         * Runs every 60 seconds. If the token expires within kProactiveRefreshWindowSecs,
         * triggers a silent refresh. Does nothing if no token is stored or if a refresh
         * is already in flight.
         */
        auto OnProactiveTimerTick() -> void;

        /**
         * @brief Start the proactive refresh timer.
         * @details Schedules OnProactiveTimerTick() every kTimerIntervalMs.
         */
        auto StartProactiveTimer() -> void;

        // ==================== Core Refresh Logic ====================

        /**
         * @brief Execute the actual refresh network call.
         *
         * @details
         * Called only when m_refresh_in_flight is false. Sets the flag to true,
         * issues the AuthService::Refresh call, and on completion resolves all
         * queued waiters.
         */
        auto DoRefresh() -> void;

        /**
         * @brief Resolve all waiting callbacks with the given result.
         *
         * @param success  Whether the refresh succeeded.
         */
        auto ResolveWaiters(bool success) -> void;

        /**
         * @brief Handle a failed refresh: clear tokens, emit forceLogout.
         */
        auto OnRefreshFailed() -> void;

        // ==================== Constants ====================

        /// Proactive refresh window: refresh when token expires within this many seconds.
        static constexpr int kProactiveRefreshWindowSecs = 5 * 60; // 5 minutes

        /// Timer interval for proactive expiry checks.
        static constexpr int kTimerIntervalMs = 60 * 1000; // 60 seconds

        // ==================== Dependencies ====================

        AuthService* m_auth_service;
        TokenStore* m_token_store;
        api::ApiClient* m_api_client;

        // ==================== State ====================

        QTimer m_proactive_timer;
        QMutex m_mutex;
        bool m_refresh_in_flight{ false };
        std::vector<RefreshDoneCallback> m_waiters;
    };

} // namespace disk::qml::services
