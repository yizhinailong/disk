/**
 * @file SettingsViewModel.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief SettingsViewModel implementation
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "SettingsViewModel.hpp"

#include <QJSEngine>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QUrl>

#include <api/ApiClient.hpp>
#include <platform/PlatformIntegration.hpp>
#include <utils/ConfigStore.hpp>

namespace disk::qml::viewmodels {

    // ==================== Construction ====================

    SettingsViewModel::SettingsViewModel(
        utils::ConfigStore* configStore,
        api::ApiClient* apiClient,
        platform::PlatformIntegration* platformIntegration,
        QObject* parent
    ) : QObject(parent),
        m_config_store(configStore),
        m_api_client(apiClient),
        m_platform_integration(platformIntegration) {
        LoadFromStore();
    }

    // ==================== Singleton ====================

    auto SettingsViewModel::SetInstance(SettingsViewModel* instance) -> void {
        s_instance = instance;
    }

    auto SettingsViewModel::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> SettingsViewModel* {
        Q_ASSERT(s_instance != nullptr);
        Q_ASSERT(qmlEngine->thread() == s_instance->thread());

        if (s_engine) {
            Q_ASSERT(jsEngine == s_engine);
        } else {
            s_engine = jsEngine;
        }

        QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
        return s_instance;
    }

    // ==================== Property Getters ====================

    auto SettingsViewModel::ServerUrl() const -> const QString& {
        return m_server_url;
    }

    auto SettingsViewModel::DownloadDir() const -> const QString& {
        return m_download_dir;
    }

    auto SettingsViewModel::ConcurrentUploads() const -> int {
        return m_concurrent_uploads;
    }

    auto SettingsViewModel::ConcurrentDownloads() const -> int {
        return m_concurrent_downloads;
    }

    auto SettingsViewModel::AutoStart() const -> bool {
        return m_auto_start;
    }

    auto SettingsViewModel::MinimizeToTray() const -> bool {
        return m_minimize_to_tray;
    }

    auto SettingsViewModel::ShowNotifications() const -> bool {
        return m_show_notifications;
    }

    auto SettingsViewModel::ConfirmDelete() const -> bool {
        return m_confirm_delete;
    }

    auto SettingsViewModel::HasUnsavedChanges() const -> bool {
        return m_has_unsaved_changes;
    }

    auto SettingsViewModel::ErrorMessage() const -> const QString& {
        return m_error_message;
    }

    // ==================== Property Setters ====================

    auto SettingsViewModel::SetServerUrl(const QString& url) -> void {
        if (m_server_url == url) {
            return;
        }
        m_server_url = url;
        emit serverUrlChanged();
        UpdateHasUnsavedChanges();
    }

    auto SettingsViewModel::SetDownloadDir(const QString& dir) -> void {
        if (m_download_dir == dir) {
            return;
        }
        m_download_dir = dir;
        emit downloadDirChanged();
        UpdateHasUnsavedChanges();
    }

    auto SettingsViewModel::SetConcurrentUploads(int value) -> void {
        const int clamped = qBound(1, value, 10);
        if (m_concurrent_uploads == clamped) {
            return;
        }
        m_concurrent_uploads = clamped;
        emit concurrentUploadsChanged();
        UpdateHasUnsavedChanges();
    }

    auto SettingsViewModel::SetConcurrentDownloads(int value) -> void {
        const int clamped = qBound(1, value, 10);
        if (m_concurrent_downloads == clamped) {
            return;
        }
        m_concurrent_downloads = clamped;
        emit concurrentDownloadsChanged();
        UpdateHasUnsavedChanges();
    }

    auto SettingsViewModel::SetAutoStart(bool value) -> void {
        if (m_auto_start == value) {
            return;
        }
        m_auto_start = value;
        emit autoStartChanged();
        UpdateHasUnsavedChanges();
    }

    auto SettingsViewModel::SetMinimizeToTray(bool value) -> void {
        if (m_minimize_to_tray == value) {
            return;
        }
        m_minimize_to_tray = value;
        emit minimizeToTrayChanged();
        UpdateHasUnsavedChanges();
    }

    auto SettingsViewModel::SetShowNotifications(bool value) -> void {
        if (m_show_notifications == value) {
            return;
        }
        m_show_notifications = value;
        emit showNotificationsChanged();
        UpdateHasUnsavedChanges();
    }

    auto SettingsViewModel::SetConfirmDelete(bool value) -> void {
        if (m_confirm_delete == value) {
            return;
        }
        m_confirm_delete = value;
        emit confirmDeleteChanged();
        UpdateHasUnsavedChanges();
    }

    // ==================== Actions ====================

    void SettingsViewModel::save() {
        // Validate server URL
        if (!ValidateServerUrl(m_server_url)) {
            SetErrorMessage(QStringLiteral("服务器地址格式无效，请输入有效的 URL"));
            return;
        }

        // Validate concurrent counts (already clamped by setters, but be defensive)
        if (m_concurrent_uploads < 1 || m_concurrent_uploads > 10) {
            SetErrorMessage(QStringLiteral("并发上传数必须在 1-10 之间"));
            return;
        }
        if (m_concurrent_downloads < 1 || m_concurrent_downloads > 10) {
            SetErrorMessage(QStringLiteral("并发下载数必须在 1-10 之间"));
            return;
        }

        // Clear any previous error
        SetErrorMessage(QString{});

        // Detect if server URL changed
        const bool serverUrlChanged = (m_server_url != m_saved.serverUrl);

        // Persist all settings
        m_config_store->SetServerUrl(QUrl(m_server_url));
        m_config_store->SetDownloadDir(m_download_dir);
        m_config_store->SetConcurrentUploads(m_concurrent_uploads);
        m_config_store->SetConcurrentDownloads(m_concurrent_downloads);
        m_config_store->SetAutoStart(m_auto_start);
        m_config_store->SetMinimizeToTray(m_minimize_to_tray);
        m_config_store->SetShowNotifications(m_show_notifications);
        m_config_store->SetConfirmDelete(m_confirm_delete);

        // Apply platform-specific runtime integration
        if (m_platform_integration) {
            // Apply auto-start setting
            if (m_platform_integration->IsAutoStartSupported()) {
                m_platform_integration->SetAutoStart(m_auto_start);
            }
            // Note: minimizeToTray requires QWindow* which is not available here
            // Will be handled by MainWindow integration (Task 10)
        }
        // Update ApiClient base URL if changed
        if (serverUrlChanged) {
            m_api_client->SetBaseUrl(QUrl(m_server_url));
        }

        // Update saved snapshot
        m_saved = SettingsSnapshot{
            .serverUrl = m_server_url,
            .downloadDir = m_download_dir,
            .concurrentUploads = m_concurrent_uploads,
            .concurrentDownloads = m_concurrent_downloads,
            .autoStart = m_auto_start,
            .minimizeToTray = m_minimize_to_tray,
            .showNotifications = m_show_notifications,
            .confirmDelete = m_confirm_delete,
        };

        UpdateHasUnsavedChanges();
        emit settingsSaved();
    }

    void SettingsViewModel::resetDefaults() {
        SetServerUrl(QStringLiteral("http://127.0.0.1:8080"));
        SetDownloadDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
        SetConcurrentUploads(utils::ConfigStore::kDefaultConcurrentUploads);
        SetConcurrentDownloads(utils::ConfigStore::kDefaultConcurrentDownloads);
        SetAutoStart(false);
        SetMinimizeToTray(false);
        SetShowNotifications(true);
        SetConfirmDelete(true);
        SetErrorMessage(QString{});
    }

    void SettingsViewModel::revert() {
        SetServerUrl(m_saved.serverUrl);
        SetDownloadDir(m_saved.downloadDir);
        SetConcurrentUploads(m_saved.concurrentUploads);
        SetConcurrentDownloads(m_saved.concurrentDownloads);
        SetAutoStart(m_saved.autoStart);
        SetMinimizeToTray(m_saved.minimizeToTray);
        SetShowNotifications(m_saved.showNotifications);
        SetConfirmDelete(m_saved.confirmDelete);
        SetErrorMessage(QString{});
    }

    // ==================== Private Helpers ====================

    auto SettingsViewModel::LoadFromStore() -> void {
        m_server_url = m_config_store->ServerUrl().toString();
        m_download_dir = m_config_store->DownloadDir();
        m_concurrent_uploads = m_config_store->ConcurrentUploads();
        m_concurrent_downloads = m_config_store->ConcurrentDownloads();
        m_auto_start = m_config_store->AutoStart();
        m_minimize_to_tray = m_config_store->MinimizeToTray();
        m_show_notifications = m_config_store->ShowNotifications();
        m_confirm_delete = m_config_store->ConfirmDelete();

        // Initialize saved snapshot
        m_saved = SettingsSnapshot{
            .serverUrl = m_server_url,
            .downloadDir = m_download_dir,
            .concurrentUploads = m_concurrent_uploads,
            .concurrentDownloads = m_concurrent_downloads,
            .autoStart = m_auto_start,
            .minimizeToTray = m_minimize_to_tray,
            .showNotifications = m_show_notifications,
            .confirmDelete = m_confirm_delete,
        };

        m_has_unsaved_changes = false;
    }

    auto SettingsViewModel::UpdateHasUnsavedChanges() -> void {
        const bool dirty =
            m_server_url != m_saved.serverUrl ||
            m_download_dir != m_saved.downloadDir ||
            m_concurrent_uploads != m_saved.concurrentUploads ||
            m_concurrent_downloads != m_saved.concurrentDownloads ||
            m_auto_start != m_saved.autoStart ||
            m_minimize_to_tray != m_saved.minimizeToTray ||
            m_show_notifications != m_saved.showNotifications ||
            m_confirm_delete != m_saved.confirmDelete;

        if (m_has_unsaved_changes == dirty) {
            return;
        }
        m_has_unsaved_changes = dirty;
        emit hasUnsavedChangesChanged();
    }

    auto SettingsViewModel::SetErrorMessage(const QString& message) -> void {
        if (m_error_message == message) {
            return;
        }
        m_error_message = message;
        emit errorMessageChanged();
    }

    auto SettingsViewModel::ValidateServerUrl(const QString& url) const -> bool {
        if (url.trimmed().isEmpty()) {
            return false;
        }
        const QUrl parsed(url);
        return parsed.isValid() && (parsed.scheme() == QStringLiteral("http") || parsed.scheme() == QStringLiteral("https")) && !parsed.host().isEmpty();
    }

} // namespace disk::qml::viewmodels
