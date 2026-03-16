/**
 * @file ConfigStore.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 通过 QSettings 持久化客户端配置
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
     * @brief 使用 QSettings 持久化和检索客户端配置。
     * @details 配置存储在 @c config、@c transfers 和 @c ui 组下。
     *          管理的键：
     *   - @c config/serverUrl        （默认值：@c http://127.0.0.1:8080）
     *   - @c transfers/downloadDir   （默认值：QStandardPaths::DownloadLocation）
     *   - @c transfers/concurrentUploads   （默认值：3，范围 [1, 10]）
     *   - @c transfers/concurrentDownloads （默认值：3，范围 [1, 10]）
     *   - @c ui/autoStart            （默认值：false）
     *   - @c ui/minimizeToTray       （默认值：false）
     *   - @c ui/showNotifications    （默认值：true）
     *   - @c ui/confirmDelete        （默认值：true）
     */
    class ConfigStore {
    public:
        ConfigStore();

        /**
         * @brief 返回配置的服务器基础 URL。
         * @return 从 QSettings 键 @c config/serverUrl 读取的 QUrl，
         *         未设置时默认为 @c http://127.0.0.1:8080。
         */
        auto ServerUrl() const -> QUrl;
        /**
         * @brief 持久化新的服务器基础 URL。
         * @param url  要存储到 QSettings 键 @c config/serverUrl 的 URL。
         */
        auto SetServerUrl(const QUrl& url) -> void;

        static constexpr int kDefaultConcurrentUploads = 3;
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
