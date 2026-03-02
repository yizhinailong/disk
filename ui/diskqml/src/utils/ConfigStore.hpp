/**
 * @file ConfigStore.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Client configuration persistence via QSettings
 * @version 0.1
 * @date 2026-03-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <QSettings>
#include <QUrl>

namespace disk::qml::utils {

    /**
     * @brief Persists and retrieves client configuration using QSettings.
     * @details Settings are stored under the group @c config.
     *          Currently managed keys:
     *   - @c serverUrl  (default: @c http://127.0.0.1:8080)
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

    private:
        mutable QSettings m_settings;
    };

} // namespace disk::qml::utils
