/**
 * @file PlatformIntegration.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Platform-specific integration implementation
 * @version 0.1
 * @date 2026-03-10
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "PlatformIntegration.hpp"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QSettings>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QWindow>

#ifdef Q_OS_WIN
    #include <QCoreApplication>
#endif

#ifdef Q_OS_LINUX
    #include <QCoreApplication>
#endif

namespace disk::qml::platform {

    class PlatformIntegration::Impl {
    public:
        Impl() : m_tray_icon(nullptr), m_minimize_to_tray_enabled(false), m_main_window(nullptr) {}

        ~Impl() {
            if (m_tray_icon) {
                delete m_tray_icon;
                m_tray_icon = nullptr;
            }
        }

        QSystemTrayIcon* m_tray_icon;
        bool m_minimize_to_tray_enabled;
        QWindow* m_main_window;
    };

    PlatformIntegration::PlatformIntegration(QObject* parent)
        : QObject(parent), m_impl(new Impl()) {}

    PlatformIntegration::~PlatformIntegration() {
        delete m_impl;
    }

    auto PlatformIntegration::IsAutoStartSupported() const -> bool {
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
        return true;
#else
        return false;
#endif
    }

    auto PlatformIntegration::IsSystemTraySupported() const -> bool {
        return QSystemTrayIcon::isSystemTrayAvailable();
    }

    auto PlatformIntegration::AreNotificationsSupported() const -> bool {
        return QSystemTrayIcon::supportsMessages();
    }

    auto PlatformIntegration::SetAutoStart(bool enabled) -> bool {
#if defined(Q_OS_WIN)
        QSettings settings(
            QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
            QSettings::NativeFormat
        );
        const QString appName = QCoreApplication::applicationName();
        const QString appPath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());

        if (enabled) {
            settings.setValue(appName, appPath);
            if (settings.status() == QSettings::NoError) {
                qInfo() << "[PlatformIntegration] Auto-start enabled (Windows)";
                return true;
            } else {
                qWarning() << "[PlatformIntegration] Failed to enable auto-start (Windows)";
                return false;
            }
        } else {
            settings.remove(appName);
            if (settings.status() == QSettings::NoError) {
                qInfo() << "[PlatformIntegration] Auto-start disabled (Windows)";
                return true;
            } else {
                qWarning() << "[PlatformIntegration] Failed to disable auto-start (Windows)";
                return false;
            }
        }

#elif defined(Q_OS_LINUX)
        const QString desktopFileContent = QStringLiteral(
                                               "[Desktop Entry]\n" "Type=Application\n" "Name=diskqml\n" "Exec="
                                           ) +
                                           QCoreApplication::applicationFilePath() + QStringLiteral("\n" "Icon=diskqml\n" "Comment=Disk Cloud Storage Client\n" "Terminal=false\n" "Categories=Network;FileTransfer;\n");

        const QString autostartDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/autostart");
        const QString autostartFile = autostartDir + QStringLiteral("/diskqml.desktop");

        if (enabled) {
            QDir dir(autostartDir);
            if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
                qWarning() << "[PlatformIntegration] Failed to create autostart directory (Linux)";
                return false;
            }

            QFile file(autostartFile);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                file.write(desktopFileContent.toUtf8());
                file.close();
                qInfo() << "[PlatformIntegration] Auto-start enabled (Linux)";
                return true;
            } else {
                qWarning() << "[PlatformIntegration] Failed to write autostart file (Linux)";
                return false;
            }
        } else {
            if (QFile::remove(autostartFile)) {
                qInfo() << "[PlatformIntegration] Auto-start disabled (Linux)";
                return true;
            } else {
                qWarning() << "[PlatformIntegration] Failed to remove autostart file (Linux)";
                return false;
            }
        }

#else
        qWarning() << "[PlatformIntegration] Auto-start not supported on this platform";
        Q_UNUSED(enabled)
        return false;
#endif
    }

    auto PlatformIntegration::SetMinimizeToTray(bool enabled, QWindow* window) -> bool {
        if (!IsSystemTraySupported()) {
            qWarning() << "[PlatformIntegration] System tray not available";
            return false;
        }

        m_impl->m_minimize_to_tray_enabled = enabled;
        m_impl->m_main_window = window;

        if (enabled) {
            if (!m_impl->m_tray_icon) {
                m_impl->m_tray_icon = new QSystemTrayIcon(this);
                connect(
                    m_impl->m_tray_icon,
                    &QSystemTrayIcon::activated,
                    this,
                    &PlatformIntegration::trayIconActivated
                );
            }
            m_impl->m_tray_icon->show();
            qInfo() << "[PlatformIntegration] Minimize-to-tray enabled";
        } else {
            if (m_impl->m_tray_icon) {
                m_impl->m_tray_icon->hide();
            }
            qInfo() << "[PlatformIntegration] Minimize-to-tray disabled";
        }

        return true;
    }

    auto PlatformIntegration::SetTrayIconVisible(bool visible) -> void {
        if (m_impl->m_tray_icon) {
            m_impl->m_tray_icon->setVisible(visible);
        }
    }

    auto PlatformIntegration::RestoreWindow() -> void {
        if (!m_impl->m_main_window) {
            qWarning() << "[PlatformIntegration] No main window set for restore";
            return;
        }

        m_impl->m_main_window->show();
        m_impl->m_main_window->raise();
        m_impl->m_main_window->requestActivate();
        qInfo() << "[PlatformIntegration] Window restored from tray";
    }

    auto PlatformIntegration::HandleCloseRequest() -> bool {
        if (!m_impl->m_minimize_to_tray_enabled) {
            return false; // Should close normally
        }

        if (!m_impl->m_main_window) {
            return false; // No window to hide
        }

        // Ensure tray icon is visible when hiding to tray
        if (m_impl->m_tray_icon) {
            m_impl->m_tray_icon->show();
        }

        m_impl->m_main_window->hide();
        qInfo() << "[PlatformIntegration] Window hidden to tray";
        return true; // Handled - don't close
    }

    auto PlatformIntegration::SetMainWindow(QWindow* window) -> void {
        m_impl->m_main_window = window;
        qInfo() << "[PlatformIntegration] Main window set";
    }

    auto PlatformIntegration::ShowNotification(const QString& title, const QString& message) -> bool {
        if (!AreNotificationsSupported()) {
            qWarning() << "[PlatformIntegration] Desktop notifications not supported";
            return false;
        }

        if (!m_impl->m_tray_icon) {
            m_impl->m_tray_icon = new QSystemTrayIcon(this);
            m_impl->m_tray_icon->show();
        }

        m_impl->m_tray_icon->showMessage(title, message, QSystemTrayIcon::Information, 5000);
        qInfo() << "[PlatformIntegration] Notification shown:" << title;
        return true;
    }

} // namespace disk::qml::platform
