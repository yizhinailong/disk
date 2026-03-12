/**
 * @file SessionViewModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 会话视图模型，管理全局会话状态、存储指标和登出操作
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <QObject>
#include <QString>

#include <QtQml/qjsengine.h>
#include <QtQml/qqmlengine.h>
#include <QtQml/qqmlregistration.h>

namespace disk::qml::services {
    class AuthService;
    class TokenStore;
} // namespace disk::qml::services

namespace disk::qml::utils {
    class ConfigStore;
} // namespace disk::qml::utils

namespace disk::qml::viewmodels {

    class LoginViewModel;

    /**
     * @brief 跟踪全局会话状态并提供登出功能的 QML 单例。
     *
     * @details
     * 向 QML 暴露用户登录状态、用户名、存储配额/使用量指标和配置的服务器 URL。
     * 响应来自 LoginViewModel 的登录事件和来自 TokenStore 的令牌状态。
     * logout() 操作调用服务器 API，然后无论服务器响应如何都清除本地令牌状态。
     *
     * 单例边界审计（任务 7）：应用全局。此 ViewModel 是跨屏幕消费的
     * 共享认证/会话真实来源，因此保持为类型化 QML 单例。
     *
     * 单例生命周期：应用拥有的实例必须先通过 SetInstance() 创建并注册，
     * 然后 QML 引擎才能调用 create()。
     */
    class SessionViewModel : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_SINGLETON

        // ==================== 属性 ====================

        Q_PROPERTY(bool isLoggedIn READ IsLoggedIn NOTIFY isLoggedInChanged) ///< 本地令牌存储中存在有效访问令牌时为 true
        Q_PROPERTY(QString loggedInUserName READ LoggedInUserName NOTIFY loggedInUserNameChanged) ///< 当前登录用户的用户名；未登录时为空

        Q_PROPERTY(qint64 storageUsed READ StorageUsed NOTIFY storageUsedChanged) ///< 当前用户已使用的存储空间（字节）；未登录时为 0
        Q_PROPERTY(qint64 storageQuota READ StorageQuota NOTIFY storageQuotaChanged) ///< 当前用户分配的存储配额（字节）；未登录时为 0

        Q_PROPERTY(QString storageUsedFormatted READ StorageUsedFormatted NOTIFY storageUsedChanged) ///< 格式化的已用存储（如 "1.23 GB"）
        Q_PROPERTY(QString storageQuotaFormatted READ StorageQuotaFormatted NOTIFY storageQuotaChanged) ///< 格式化的存储配额（如 "10.00 GB"）
        Q_PROPERTY(double storagePercentage READ StoragePercentage NOTIFY storagePercentageChanged) ///< 存储使用百分比（0.0–100.0）；配额为 0 时为 0.0

        Q_PROPERTY(QString serverUrl READ ServerUrl CONSTANT) ///< 配置的服务器 URL（只读，构造时设置）
    public:
        explicit SessionViewModel(
            LoginViewModel* loginViewModel,
            services::TokenStore* tokenStore,
            services::AuthService* authService,
            utils::ConfigStore* configStore,
            QObject* parent = nullptr
        );

        // ==================== Public API ====================

        /**
         * @brief 注册预创建的实例供 QML 引擎使用。
         *
         * @details
         * 必须在 QML 引擎通过 create() 请求单例之前调用。
         * @p instance 的所有权保留在调用者（C++ 端）。
         */

        /**
         * @brief QML 单例工厂——由 QML 引擎调用一次。
         *
         * @details
         * 运行时强制约束：
         * - s_instance 必须事先通过 SetInstance() 设置。
         * - @p qmlEngine 必须与实例共享线程亲和性。
         * - 只有一个 QJSEngine 可以访问此单例；第二个引擎会触发 Q_ASSERT 失败。
         * - 所有权设置为 CppOwnership，防止引擎销毁时删除实例。
         */

        [[nodiscard]] auto IsLoggedIn() const -> bool;
        [[nodiscard]] auto LoggedInUserName() const -> const QString&;
        [[nodiscard]] auto StorageUsed() const -> qint64;
        [[nodiscard]] auto StorageQuota() const -> qint64;
        [[nodiscard]] auto StorageUsedFormatted() const -> QString;
        [[nodiscard]] auto StorageQuotaFormatted() const -> QString;
        [[nodiscard]] auto StoragePercentage() const -> double;
        [[nodiscard]] auto ServerUrl() const -> const QString&;

        /**
         * @brief 发送登出请求并清除本地会话状态。
         *
         * @details
         * 通过 AuthService::Logout() 将当前访问令牌发送到服务器。
         * 无论服务器响应如何，都清除 TokenStore、重置登录用户名
         * 并重新评估 isLoggedIn。
         *
         * 使用子上下文 QObject，以便在此 ViewModel 在响应到达前被销毁时
         * 自动丢弃回调。
         */

        // ==================== Signals ====================

    signals:
        void isLoggedInChanged();
        void loggedInUserNameChanged();
        void storageUsedChanged();
        void storageQuotaChanged();
        void storagePercentageChanged();

    private:
        // ==================== 私有辅助方法 ====================

        auto SetLoggedInUserName(const QString& name) -> void;
        auto UpdateIsLoggedIn() -> void;
        auto SetStorageUsed(qint64 bytes) -> void;
        auto SetStorageQuota(qint64 bytes) -> void;
        static auto FormatBytes(qint64 bytes) -> QString;

        // ==================== 状态 ====================

        LoginViewModel* m_login_view_model; ///< 登录视图模型
        services::TokenStore* m_token_store; ///< 令牌存储
        services::AuthService* m_auth_service; ///< 认证服务

        bool m_is_logged_in{ false }; ///< 是否已登录
        QString m_logged_in_user_name; ///< 已登录用户名
        qint64 m_storage_used{ 0 }; ///< 已使用存储空间
        qint64 m_storage_quota{ 0 }; ///< 存储配额
        QString m_server_url; ///< 服务器 URL

        inline static SessionViewModel* s_instance = nullptr; ///< 单例实例
        inline static QJSEngine* s_engine = nullptr; ///< JS 引擎实例

} // namespace disk::qml::viewmodels
