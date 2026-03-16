/**
 * @file PlatformIntegration.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 平台特定集成：系统托盘、自启动和通知
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef DISK_QML_PLATFORM_INTEGRATION_HPP
#define DISK_QML_PLATFORM_INTEGRATION_HPP

#include <QSystemTrayIcon>

class QWindow;

namespace disk::qml::platform {

    /**
     * @brief 操作系统特定功能的平台集成。
     *
     * 提供以下运行时集成：
     * - 自启动：在操作系统启动项中注册/注销应用程序
     * - 系统托盘：最小化到托盘和托盘图标管理
     * - 通知：通过原生 API 显示桌面通知
     *
     * 平台支持矩阵：
     * | 功能       | Windows | Linux (XDG) | macOS |
     * |-----------|---------|-------------|-------|
     * | 自启动     | ✓       | ✓           | 待定   |
     * | 系统托盘   | ✓       | ✓           | 待定   |
     * | 通知       | ✓       | ✓           | 待定   |
     *
     * 不支持的平台会产生明确的警告日志并无操作。
     */
    class PlatformIntegration : public QObject {
        Q_OBJECT

    public:
        explicit PlatformIntegration(QObject* parent = nullptr);
        ~PlatformIntegration() override;

        // 禁止复制
        PlatformIntegration(const PlatformIntegration&) = delete;
        auto operator=(const PlatformIntegration&) -> PlatformIntegration& = delete;
        PlatformIntegration(PlatformIntegration&&) = delete;
        auto operator=(PlatformIntegration&&) -> PlatformIntegration& = delete;

        // ==================== 功能可用性 ====================

        /**
         * @brief 检查当前平台是否支持自启动。
         * @return 如果可以配置自启动则返回 true
         */
        [[nodiscard]] auto IsAutoStartSupported() const -> bool;

        /**
         * @brief 检查当前平台是否支持系统托盘。
         * @return 如果系统托盘集成可用则返回 true
         */
        [[nodiscard]] auto IsSystemTraySupported() const -> bool;

        /**
         * @brief 检查是否支持桌面通知。
         * @return 如果可以显示通知则返回 true
         */
        [[nodiscard]] auto AreNotificationsSupported() const -> bool;

        // ==================== 自启动 ====================

        /**
         * @brief 启用或禁用系统启动时的自启动。
         * @param enabled true 启用自启动，false 禁用
         * @return 操作成功返回 true，失败或不支持返回 false
         */
        auto SetAutoStart(bool enabled) -> bool;

        // ==================== 系统托盘 ====================

        /**
         * @brief 启用或禁用最小化到托盘行为。
         * @param enabled true 最小化到托盘，false 正常最小化
         * @param window 主应用窗口（用于事件过滤）
         * @return 操作成功返回 true，失败或不支持返回 false
         */
        auto SetMinimizeToTray(bool enabled, QWindow* window) -> bool;

        /**
         * @brief 显示或隐藏托盘图标。
         * @param visible true 显示托盘图标，false 隐藏
         */
        auto SetTrayIconVisible(bool visible) -> void;

        /**
         * @brief 从最小化/隐藏状态恢复主窗口。
         *
         * 显示、置顶并激活窗口。点击托盘图标时使用。
         */
        auto RestoreWindow() -> void;

        /**
         * @brief 处理窗口关闭请求 - 如果启用则隐藏到托盘。
         * @return 如果窗口已隐藏到托盘返回 true，应正常关闭返回 false
         */
        auto HandleCloseRequest() -> bool;

        /**
         * @brief 设置托盘操作的主窗口。
         * @param window 主应用窗口
         */
        auto SetMainWindow(QWindow* window) -> void;

        // ==================== 通知 ====================

        /**
         * @brief 显示桌面通知。
         * @param title 通知标题
         * @param message 通知正文
         * @return 通知已显示返回 true，失败或不支持返回 false
         */
        auto ShowNotification(const QString& title, const QString& message) -> bool;

    signals:
        /**
         * @brief 托盘图标被激活（如点击）时发射。
         * @param reason 激活原因
         */
        void trayIconActivated(QSystemTrayIcon::ActivationReason reason);

        /**
         * @brief 当窗口应从托盘恢复时发射。
         */
        void restoreWindowRequested();

    private:
        class Impl;
        Impl* m_impl;
    };

} // namespace disk::qml::platform

#endif // DISK_QML_PLATFORM_INTEGRATION_HPP
