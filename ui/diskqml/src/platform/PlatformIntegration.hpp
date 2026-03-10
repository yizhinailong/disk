/**
 * @file PlatformIntegration.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Platform-specific integration for system tray, autostart, and notifications
 * @version 0.1
 * @date 2026-03-10
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
     * @brief Platform integration for OS-specific features.
     *
     * Provides runtime integration for:
     * - Auto-start: Register/unregister application in OS startup
     * - System tray: Minimize to tray and tray icon management
     * - Notifications: Desktop notifications via native APIs
     *
     * Platform support matrix:
     * | Feature       | Windows | Linux (XDG) | macOS |
     * |---------------|---------|-------------|-------|
     * | Auto-start    | ✓       | ✓           | TBD   |
     * | System tray   | ✓       | ✓           | TBD   |
     * | Notifications | ✓       | ✓           | TBD   |
     *
     * Unsupported platforms yield explicit warning logs and no-op behavior.
     */
    class PlatformIntegration : public QObject {
        Q_OBJECT

    public:
        explicit PlatformIntegration(QObject* parent = nullptr);
        ~PlatformIntegration() override;

        // Prevent copying
        PlatformIntegration(const PlatformIntegration&) = delete;
        auto operator=(const PlatformIntegration&) -> PlatformIntegration& = delete;
        PlatformIntegration(PlatformIntegration&&) = delete;
        auto operator=(PlatformIntegration&&) -> PlatformIntegration& = delete;

        // ==================== Feature Availability ====================

        /**
         * @brief Check if auto-start is supported on current platform.
         * @return true if auto-start can be configured
         */
        [[nodiscard]] auto IsAutoStartSupported() const -> bool;

        /**
         * @brief Check if system tray is available on current platform.
         * @return true if system tray integration is available
         */
        [[nodiscard]] auto IsSystemTraySupported() const -> bool;

        /**
         * @brief Check if desktop notifications are supported.
         * @return true if notifications can be shown
         */
        [[nodiscard]] auto AreNotificationsSupported() const -> bool;

        // ==================== Auto-start ====================

        /**
         * @brief Enable or disable auto-start on system boot.
         * @param enabled true to enable auto-start, false to disable
         * @return true if operation succeeded, false if failed or unsupported
         */
        auto SetAutoStart(bool enabled) -> bool;

        // ==================== System Tray ====================

        /**
         * @brief Enable or disable minimize-to-tray behavior.
         * @param enabled true to minimize to tray, false for normal minimize
         * @param window the main application window (for event filtering)
         * @return true if operation succeeded, false if failed or unsupported
         */
        auto SetMinimizeToTray(bool enabled, QWindow* window) -> bool;

        /**
         * @brief Show or hide the tray icon.
         * @param visible true to show tray icon, false to hide
         */
        auto SetTrayIconVisible(bool visible) -> void;

        /**
         * @brief Restore the main window from minimized/hidden state.
         *
         * Shows, raises, and activates the window. Used when clicking tray icon.
         */
        auto RestoreWindow() -> void;

        /**
         * @brief Handle window close request - hide to tray if enabled.
         * @return true if window was hidden to tray, false if should close normally
         */
        auto HandleCloseRequest() -> bool;

        /**
         * @brief Set the main window for tray operations.
         * @param window the main application window
         */
        auto SetMainWindow(QWindow* window) -> void;

        // ==================== Notifications ====================

        /**
         * @brief Show a desktop notification.
         * @param title notification title
         * @param message notification body text
         * @return true if notification was shown, false if failed or unsupported
         */
        auto ShowNotification(const QString& title, const QString& message) -> bool;

    signals:
        /**
         * @brief Emitted when the tray icon is activated (e.g., clicked).
         * @param reason the activation reason
         */
        void trayIconActivated(QSystemTrayIcon::ActivationReason reason);

        /**
         * @brief Emitted when the window should be restored from tray.
         */
        void restoreWindowRequested();

    private:
        class Impl;
        Impl* m_impl;
    };

} // namespace disk::qml::platform

#endif // DISK_QML_PLATFORM_INTEGRATION_HPP
