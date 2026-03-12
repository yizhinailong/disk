/**
 * @file LoginViewModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 登录视图模型，管理登录表单状态
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
}

namespace disk::qml::viewmodels {

    /**
     * @brief 登录表单的 QML 单例视图模型。
     *
     * @details
     * 通过 Q_PROPERTY 绑定向 QML 暴露登录表单状态（账号、密码、加载中、错误）。
     * 业务逻辑和 API 调用完全在 C++ 中处理；QML 仅驱动 UI。
     *
     * 单例边界审计（任务 7）：页面作用域（登录表单状态）。
     * 暂时保留 QML_SINGLETON 以保持类型注册和当前导入；
     * 计划迁移目标为显式页面级实例化。
     *
     * 单例生命周期：应用拥有的实例必须先通过 SetInstance() 创建并注册，
     * 然后 QML 引擎才能调用 create()。
     */
    class LoginViewModel : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_SINGLETON

        // ==================== 属性 ====================

        Q_PROPERTY(QString account READ Account WRITE SetAccount NOTIFY accountChanged) ///< 登录账号（用户名或邮箱）
        Q_PROPERTY(QString password READ Password WRITE SetPassword NOTIFY passwordChanged) ///< 登录密码
        Q_PROPERTY(bool loading READ Loading NOTIFY loadingChanged) ///< 登录 API 请求进行中时为 true
        Q_PROPERTY(QString errorMessage READ ErrorMessage NOTIFY errorMessageChanged) ///< 登录失败时的错误消息；clearError() 或新的 submit() 会清除
        Q_PROPERTY(bool canSubmit READ CanSubmit NOTIFY canSubmitChanged) ///< 账号和密码非空且无请求进行中时为 true
    public:
        explicit LoginViewModel(services::AuthService* authService, QObject* parent = nullptr);

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

        [[nodiscard]] auto Account() const -> const QString&;
        [[nodiscard]] auto Password() const -> const QString&;
        [[nodiscard]] auto Loading() const -> bool;
        [[nodiscard]] auto ErrorMessage() const -> const QString&;
        [[nodiscard]] auto CanSubmit() const -> bool;

        auto SetAccount(const QString& account) -> void;
        auto SetPassword(const QString& password) -> void;

        /**
         * @brief 提交登录表单。
         *
         * @details
         * 如果 canSubmit 为 false 或请求正在进行中则无操作。
         * 设置 loading=true 并清除之前的错误，然后调用 AuthService::Login()。
         * 成功时发射 loginSucceeded(username, storageUsed, storageQuota)。
         * 失败时设置 errorMessage 并清除 loading。
         *
         * 使用子上下文 QObject，以便在此 ViewModel 在响应到达前被销毁时
         * 自动丢弃回调。
         */

        /**
         * @brief 清除当前错误消息。
         *
         * @details
         * 将 errorMessage 重置为空字符串。当用户在失败尝试后编辑字段
         * 且 UI 想要隐藏错误横幅时很有用。
         */

        // ==================== Signals ====================

    signals:
        void accountChanged();
        void passwordChanged();
        void loadingChanged();
        void errorMessageChanged();
        void canSubmitChanged();
        void loginSucceeded(const QString& username, quint64 storageUsed, quint64 storageQuota);

    private:
        // ==================== 私有辅助方法 ====================

        auto UpdateCanSubmit() -> void;
        auto SetLoading(bool loading) -> void;
        auto SetErrorMessage(const QString& message) -> void;

        // ==================== 状态 ====================

        services::AuthService* m_auth_service; ///< 认证服务
        QString m_account; ///< 账号
        QString m_password; ///< 密码
        bool m_loading{ false }; ///< 是否正在加载
        QString m_error_message; ///< 错误消息
        bool m_can_submit{ false }; ///< 是否可提交

        inline static LoginViewModel* s_instance = nullptr; ///< 单例实例
        inline static QJSEngine* s_engine = nullptr; ///< JS 引擎实例

} // namespace disk::qml::viewmodels
