/**
 * @file ConfigStore.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Client configuration persistence via QSettings
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QUrl>

namespace disk::qml::utils {

    /**
     * @brief Persists and retrieves client configuration using QSettings.
     * @details Settings are stored under the groups @c config, @c transfers, and @c ui.
     *          Managed keys:
     *   - @c config/serverUrl        (default: @c http://127.0.0.1:8080)
     *   - @c transfers/downloadDir   (default: QStandardPaths::DownloadLocation)
     *   - @c transfers/concurrentUploads   (default: 3, range [1, 10])
     *   - @c transfers/concurrentDownloads (default: 3, range [1, 10])
     *   - @c ui/autoStart            (default: false)
     *   - @c ui/minimizeToTray       (default: false)
     *   - @c ui/showNotifications    (default: true)
     *   - @c ui/confirmDelete        (default: true)
     */
    class ConfigStore {
    public:
        ConfigStore();

        /**
         * @brief Return the configured server base URL.
         * @return A QUrl read from QSettings key @c config/serverUrl,
         *         defaulting to @c http://127.0.0.1:8080 when not set.
         */
        auto ServerUrl() const -> QUrl;
        /**
         * @brief Persist a new server base URL.
         * @param url  The URL to store under QSettings key @c config/serverUrl.
         */
        auto SetServerUrl(const QUrl& url) -> void;

        static constexpr int kDefaultConcurrentUploads   = 3;
        static constexpr int kDefaultConcurrentDownloads = 3;

        [[nodiscard]] auto ConcurrentUploads() const -> int;
        auto SetConcurrentUploads(int value) -> void;

        [[nodiscard]] auto ConcurrentDownloads() const -> int;
        auto SetConcurrentDownloads(int value) -> void;

        // ==================== Downloads ====================

        [[nodiscard]] auto DownloadDir() const -> QString;
        auto SetDownloadDir(const QString& path) -> void;

        // ==================== UI Preferences ====================

        [[nodiscard]] auto AutoStart() const -> bool;
        auto SetAutoStart(bool value) -> void;

        [[nodiscard]] auto MinimizeToTray() const -> bool;
        auto SetMinimizeToTray(bool value) -> void;

        [[nodiscard]] auto ShowNotifications() const -> bool;
        auto SetShowNotifications(bool value) -> void;

        [[nodiscard]] auto ConfirmDelete() const -> bool;
        auto SetConfirmDelete(bool value) -> void;

    private:
        mutable QSettings m_settings;
    };

} // namespace disk::qml::utils
