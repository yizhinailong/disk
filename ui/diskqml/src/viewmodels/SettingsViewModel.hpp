/**
 * @file SettingsViewModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 设置视图模型，管理客户端设置表单
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
     * @brief 客户端设置的 QML 单例视图模型。
     *
     * @details
     * 通过 Q_PROPERTY 绑定向 QML 暴露可编辑的设置字段。
     * 通过比较当前值与持久化值跟踪未保存的更改。
     * save() 时写入 ConfigStore，如果服务器 URL 更改则更新 ApiClient 基础 URL。
     *
     * 单例边界审计（任务 7）：页面作用域（设置表单状态）。
     * 暂时保留 QML_SINGLETON 以保持类型注册和现有导入；
     * 未来方向为显式页面级实例化/注入。
     *
     * 单例生命周期：应用拥有的实例必须先通过 SetInstance() 创建并注册，
     * 然后 QML 引擎才能调用 create()。
     */
    class SettingsViewModel : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_SINGLETON

        // ==================== 连接设置 ====================

        Q_PROPERTY(QString serverUrl READ ServerUrl WRITE SetServerUrl NOTIFY serverUrlChanged) ///< 可编辑的服务器 URL 字符串

        // ==================== 传输设置 ====================

        Q_PROPERTY(QString downloadDir READ DownloadDir WRITE SetDownloadDir NOTIFY downloadDirChanged)                             ///< 可编辑的下载目录路径
        Q_PROPERTY(int concurrentUploads READ ConcurrentUploads WRITE SetConcurrentUploads NOTIFY concurrentUploadsChanged)         ///< 可编辑的并发上传数 [1, 10]
        Q_PROPERTY(int concurrentDownloads READ ConcurrentDownloads WRITE SetConcurrentDownloads NOTIFY concurrentDownloadsChanged) ///< 可编辑的并发下载数 [1, 10]

        // ==================== UI 偏好 ====================

        Q_PROPERTY(bool autoStart READ AutoStart WRITE SetAutoStart NOTIFY autoStartChanged)                                 ///< 系统启动时启动
        Q_PROPERTY(bool minimizeToTray READ MinimizeToTray WRITE SetMinimizeToTray NOTIFY minimizeToTrayChanged)             ///< 最小化到系统托盘而非关闭
        Q_PROPERTY(bool showNotifications READ ShowNotifications WRITE SetShowNotifications NOTIFY showNotificationsChanged) ///< 显示桌面通知
        Q_PROPERTY(bool confirmDelete READ ConfirmDelete WRITE SetConfirmDelete NOTIFY confirmDeleteChanged)                 ///< 删除文件前确认

        // ==================== 派生状态 ====================

        Q_PROPERTY(bool hasUnsavedChanges READ HasUnsavedChanges NOTIFY hasUnsavedChangesChanged) ///< 任何设置与持久化值不同时为 true
        Q_PROPERTY(QString errorMessage READ ErrorMessage NOTIFY errorMessageChanged)             ///< 验证失败时的错误消息（如无效 URL 或超出范围的值）

    public:
        explicit SettingsViewModel(
            utils::ConfigStore* configStore,
            api::ApiClient* apiClient,
            platform::PlatformIntegration* platformIntegration,
            QObject* parent = nullptr
        );

        // ==================== Singleton API ====================

        /**
         * @brief 注册预创建的实例供 QML 引擎使用。
         */
        static auto SetInstance(SettingsViewModel* instance) -> void;

        /**
         * @brief QML 单例工厂——由 QML 引擎调用一次。
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
         * @brief 将所有设置保存到 ConfigStore。
         *
         * @details
         * 验证所有字段。如果验证失败，设置 errorMessage 并返回。
         * 成功时写入 ConfigStore，如果服务器 URL 更改则更新 ApiClient 基础 URL。
         * 成功时发射 settingsSaved()。
         */
        Q_INVOKABLE void save();

        /**
         * @brief 将所有字段重置为默认值。
         *
         * @details 不持久化——用户必须调用 save() 提交。
         */
        Q_INVOKABLE void resetDefaults();

        /**
         * @brief 将所有字段恢复为上次持久化的值。
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
        // ==================== 私有辅助方法 ====================

        auto LoadFromStore() -> void;
        auto UpdateHasUnsavedChanges() -> void;
        auto SetErrorMessage(const QString& message) -> void;
        [[nodiscard]] auto ValidateServerUrl(const QString& url) const -> bool;

        // ==================== 已保存（持久化）快照 ====================

        struct SettingsSnapshot {
            QString serverUrl;       ///< 服务器 URL
            QString downloadDir;     ///< 下载目录
            int concurrentUploads;   ///< 并发上传数
            int concurrentDownloads; ///< 并发下载数
            bool autoStart;          ///< 自动启动
            bool minimizeToTray;     ///< 最小化到托盘
            bool showNotifications;  ///< 显示通知
            bool confirmDelete;      ///< 确认删除
        };

        // ==================== 状态 ====================

        utils::ConfigStore* m_config_store;                    ///< 配置存储
        api::ApiClient* m_api_client;                          ///< API 客户端
        platform::PlatformIntegration* m_platform_integration; ///< 平台集成
        // 当前（可编辑）值
        QString m_server_url;              ///< 服务器 URL
        QString m_download_dir;            ///< 下载目录
        int m_concurrent_uploads{ 3 };     ///< 并发上传数
        int m_concurrent_downloads{ 3 };   ///< 并发下载数
        bool m_auto_start{ false };        ///< 自动启动
        bool m_minimize_to_tray{ false };  ///< 最小化到托盘
        bool m_show_notifications{ true }; ///< 显示通知
        bool m_confirm_delete{ true };     ///< 确认删除

        // 上次保存的快照（用于脏检查）
        SettingsSnapshot m_saved;

        bool m_has_unsaved_changes{ false };                   ///< 是否有未保存的更改
        QString m_error_message;                               ///< 错误消息

        inline static SettingsViewModel* s_instance = nullptr; ///< 单例实例
        inline static QJSEngine* s_engine = nullptr;           ///< JS 引擎实例
    };

} // namespace disk::qml::viewmodels
