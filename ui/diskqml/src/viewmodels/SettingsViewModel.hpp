/**
 * @file SettingsViewModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QML singleton ViewModel for client settings form
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <QtQml/qjsengine.h>
#include <QtQml/qqmlengine.h>
#include <QtQml/qqmlregistration.h>

namespace disk::qml::api {
    class ApiClient;
} // namespace disk::qml::api

namespace disk::qml::utils {
    class ConfigStore;
} // namespace disk::qml::utils

namespace disk::qml::platform {
    class PlatformIntegration;
} // namespace disk::qml::platform

namespace disk::qml::viewmodels {

    /**
     * @brief QML singleton ViewModel for client settings.
     *
     * @details
     * Exposes editable settings fields to QML via Q_PROPERTY bindings.
     * Tracks unsaved changes by comparing current values against persisted values.
     * On save(), writes to ConfigStore and updates ApiClient base URL if server URL changed.
     *
     * Singleton lifecycle: an application-owned instance must be created and
     * registered with SetInstance() before the QML engine calls create().
     */
    class SettingsViewModel : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_SINGLETON

        // ==================== Connection Settings ====================

        /// Editable server URL string.
        Q_PROPERTY(QString serverUrl READ ServerUrl WRITE SetServerUrl NOTIFY serverUrlChanged)

        // ==================== Transfer Settings ====================

        /// Editable download directory path.
        Q_PROPERTY(QString downloadDir READ DownloadDir WRITE SetDownloadDir NOTIFY downloadDirChanged)
        /// Editable concurrent upload count [1, 10].
        Q_PROPERTY(int concurrentUploads READ ConcurrentUploads WRITE SetConcurrentUploads NOTIFY concurrentUploadsChanged)
        /// Editable concurrent download count [1, 10].
        Q_PROPERTY(int concurrentDownloads READ ConcurrentDownloads WRITE SetConcurrentDownloads NOTIFY concurrentDownloadsChanged)

        // ==================== UI Preferences ====================

        /// Launch on system startup.
        Q_PROPERTY(bool autoStart READ AutoStart WRITE SetAutoStart NOTIFY autoStartChanged)
        /// Minimize to system tray instead of closing.
        Q_PROPERTY(bool minimizeToTray READ MinimizeToTray WRITE SetMinimizeToTray NOTIFY minimizeToTrayChanged)
        /// Show desktop notifications.
        Q_PROPERTY(bool showNotifications READ ShowNotifications WRITE SetShowNotifications NOTIFY showNotificationsChanged)
        /// Confirm before deleting files.
        Q_PROPERTY(bool confirmDelete READ ConfirmDelete WRITE SetConfirmDelete NOTIFY confirmDeleteChanged)

        // ==================== Derived State ====================

        /// True when any setting differs from persisted value.
        Q_PROPERTY(bool hasUnsavedChanges READ HasUnsavedChanges NOTIFY hasUnsavedChangesChanged)
        /// Non-empty when validation fails (e.g. invalid URL or out-of-range values).
        Q_PROPERTY(QString errorMessage READ ErrorMessage NOTIFY errorMessageChanged)

    public:
        explicit SettingsViewModel(
            utils::ConfigStore* configStore,
            api::ApiClient* apiClient,
            platform::PlatformIntegration* platformIntegration,
            QObject* parent = nullptr
        );

        // ==================== Singleton API ====================

        /**
         * @brief Register the pre-created instance for use by the QML engine.
         */
        static auto SetInstance(SettingsViewModel* instance) -> void;

        /**
         * @brief QML singleton factory — called once by the QML engine.
         */
        static auto create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> SettingsViewModel*;

        // ==================== Property Getters ====================

        [[nodiscard]] auto ServerUrl() const -> const QString&;
        [[nodiscard]] auto DownloadDir() const -> const QString&;
        [[nodiscard]] auto ConcurrentUploads() const -> int;
        [[nodiscard]] auto ConcurrentDownloads() const -> int;
        [[nodiscard]] auto AutoStart() const -> bool;
        [[nodiscard]] auto MinimizeToTray() const -> bool;
        [[nodiscard]] auto ShowNotifications() const -> bool;
        [[nodiscard]] auto ConfirmDelete() const -> bool;
        [[nodiscard]] auto HasUnsavedChanges() const -> bool;
        [[nodiscard]] auto ErrorMessage() const -> const QString&;

        // ==================== Property Setters ====================

        auto SetServerUrl(const QString& url) -> void;
        auto SetDownloadDir(const QString& dir) -> void;
        auto SetConcurrentUploads(int value) -> void;
        auto SetConcurrentDownloads(int value) -> void;
        auto SetAutoStart(bool value) -> void;
        auto SetMinimizeToTray(bool value) -> void;
        auto SetShowNotifications(bool value) -> void;
        auto SetConfirmDelete(bool value) -> void;

        // ==================== Actions ====================

        /**
         * @brief Save all settings to ConfigStore.
         *
         * @details
         * Validates all fields. If validation fails, sets errorMessage and returns.
         * On success, writes to ConfigStore and updates ApiClient base URL if changed.
         * Emits settingsSaved() on success.
         */
        Q_INVOKABLE void save();

        /**
         * @brief Reset all fields to their default values.
         *
         * @details Does not persist — user must call save() to commit.
         */
        Q_INVOKABLE void resetDefaults();

        /**
         * @brief Revert all fields to the last persisted values.
         */
        Q_INVOKABLE void revert();

        // ==================== Signals ====================

    signals:
        void serverUrlChanged();
        void downloadDirChanged();
        void concurrentUploadsChanged();
        void concurrentDownloadsChanged();
        void autoStartChanged();
        void minimizeToTrayChanged();
        void showNotificationsChanged();
        void confirmDeleteChanged();
        void hasUnsavedChangesChanged();
        void errorMessageChanged();
        void settingsSaved();

    private:
        // ==================== Private Helpers ====================

        auto LoadFromStore() -> void;
        auto UpdateHasUnsavedChanges() -> void;
        auto SetErrorMessage(const QString& message) -> void;
        [[nodiscard]] auto ValidateServerUrl(const QString& url) const -> bool;

        // ==================== Saved (persisted) snapshot ====================

        struct SettingsSnapshot {
            QString serverUrl;
            QString downloadDir;
            int concurrentUploads;
            int concurrentDownloads;
            bool autoStart;
            bool minimizeToTray;
            bool showNotifications;
            bool confirmDelete;
        };

        // ==================== State ====================

        utils::ConfigStore* m_config_store;
        api::ApiClient* m_api_client;
        platform::PlatformIntegration* m_platform_integration;
        // Current (editable) values
        QString m_server_url;
        QString m_download_dir;
        int m_concurrent_uploads{ 3 };
        int m_concurrent_downloads{ 3 };
        bool m_auto_start{ false };
        bool m_minimize_to_tray{ false };
        bool m_show_notifications{ true };
        bool m_confirm_delete{ true };

        // Last-saved snapshot for dirty checking
        SettingsSnapshot m_saved;

        bool m_has_unsaved_changes{ false };
        QString m_error_message;

        inline static SettingsViewModel* s_instance = nullptr;
        inline static QJSEngine* s_engine = nullptr;
    };

} // namespace disk::qml::viewmodels
