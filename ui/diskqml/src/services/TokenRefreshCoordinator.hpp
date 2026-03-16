/**
 * @file TokenRefreshCoordinator.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 单次飞行令牌刷新协调器，带主动刷新定时器
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
     * @brief 跨所有 API 调用方协调令牌刷新，具有单次飞行保证。
     *
     * @details
     * 职责：
     *   - 检测 API 封装中的错误码 40108（TokenExpired）
     *   - 通过 AuthService 使用存储的刷新令牌执行刷新
     *   - 确保任何时刻只有一个刷新请求在进行中；并发
     *     调用方等待并共享进行中刷新的结果
     *   - 支持重试一次：刷新成功后，原始请求
     *     回调会被重新调用并设置 `refreshed=true`，以便调用方可以重试
     *   - 运行一个主动 QTimer，在访问令牌即将过期
     *     （5 分钟内）时刷新，避免首先发生 40108 错误
     *   - 刷新失败时（40110、40111 或网络错误），清除令牌并
     *     发出 forceLogout() 信号，以便 UI 可以重定向到登录屏幕
     *
     * 线程安全：
     *   所有公共方法必须从主（GUI）线程调用。
     *   内部 QMutex 保护进行中标志和等待者列表，防止
     *   嵌套事件循环调度导致的重入。
     *
     * 所有权：
     *   不拥有任何依赖项。调用方必须确保 AuthService、TokenStore
     *   和 ApiClient 的生命周期长于此协调器。
     */
    class TokenRefreshCoordinator : public QObject {
        Q_OBJECT

    public:
        /**
         * @brief 刷新等待者的回调类型。
         * @param success  令牌是否成功刷新。
         *
         * 成功时调用方应使用 TokenStore 中的新访问令牌重试原始请求。
         * 失败时调用方应向用户传播错误（同时会发出 forceLogout 信号）。
         */
        using RefreshDoneCallback = std::function<void(bool success)>;

        /**
         * @brief 构造协调器并启动主动刷新定时器。
         *
         * @param authService  用于调用 POST /api/auth/refresh 的服务。
         * @param tokenStore   持久化令牌存储。
         * @param apiClient    共享 API 客户端（刷新时更新 bearer 令牌）。
         * @param parent       QObject 父对象，用于生命周期管理。
         */
        explicit TokenRefreshCoordinator(
            AuthService* authService,
            TokenStore* tokenStore,
            api::ApiClient* apiClient,
            QObject* parent = nullptr
        );

        /**
         * @brief 检查 API 封装是否包含 TokenExpired 错误。
         *
         * @param envelope  API 响应封装。
         * @return 如果 envelope.code == 40108（TokenExpired）则返回 true。
         */
        [[nodiscard]] static auto IsTokenExpired(const models::ApiEnvelope& envelope) -> bool;

        /**
         * @brief 请求令牌刷新（单次飞行）。
         *
         * @details
         * 如果当前没有刷新在进行中，立即发起一个。
         * 如果有刷新在进行中，回调会被排队，在进行中的刷新完成时被调用
         * （使用相同的成功/失败结果）。
         *
         * @param cb  刷新尝试完成时调用。
         */
        auto RequestRefresh(RefreshDoneCallback cb) -> void;

        /**
         * @brief 便捷方法：检查封装并在 40108 时触发刷新。
         *
         * @details
         * 如果封装不是 40108 错误，立即返回 false。
         * 如果是 40108，排队一个刷新并在完成时调用 @p cb。
         *
         * @param envelope  要检查的 API 响应封装。
         * @param cb        刷新尝试完成时调用。
         * @return 如果触发了刷新则返回 true（调用方不应处理该封装）。
         */
        auto HandleIfTokenExpired(const models::ApiEnvelope& envelope, RefreshDoneCallback cb) -> bool;

    signals:
        /**
         * @brief 令牌刷新不可恢复地失败时发出。
         *
         * @details
         * 由 SessionViewModel 连接触发完整登出 + 重定向到登录。
         * 在令牌已从 TokenStore 清除后发出。
         */
        void forceLogout();

    private:
        // ==================== 主动刷新 ====================

        /**
         * @brief 定时器回调：检查访问令牌是否在 5 分钟内过期。
         *
         * @details
         * 每 60 秒运行一次。如果令牌在 kProactiveRefreshWindowSecs 秒内过期，
         * 触发静默刷新。如果没有存储令牌或已有刷新在进行中，则不做任何操作。
         */
        auto OnProactiveTimerTick() -> void;

        /**
         * @brief 启动主动刷新定时器。
         * @details 每 kTimerIntervalMs 毫秒调度一次 OnProactiveTimerTick()。
         */
        auto StartProactiveTimer() -> void;

        // ==================== 核心刷新逻辑 ====================

        /**
         * @brief 执行实际的刷新网络调用。
         *
         * @details
         * 仅在 m_refresh_in_flight 为 false 时调用。将标志设置为 true，
         * 发起 AuthService::Refresh 调用，完成后解析所有排队的等待者。
         */
        auto DoRefresh() -> void;

        /**
         * @brief 使用给定结果解析所有等待中的回调。
         *
         * @param success  刷新是否成功。
         */
        auto ResolveWaiters(bool success) -> void;

        /**
         * @brief 处理刷新失败：清除令牌，发出 forceLogout。
         */
        auto OnRefreshFailed() -> void;

        // ==================== 常量 ====================

        /// 主动刷新窗口：当令牌在此秒数内过期时刷新。
        static constexpr int kProactiveRefreshWindowSecs = 5 * 60; // 5 分钟

        /// 主动过期检查的定时器间隔。
        static constexpr int kTimerIntervalMs = 60 * 1000; // 60 秒

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
