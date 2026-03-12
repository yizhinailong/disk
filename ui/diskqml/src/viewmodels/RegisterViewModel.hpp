/**
 * @file RegisterViewModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 注册视图模型，管理注册表单状态
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
     * @brief 注册表单的 QML 单例视图模型。
     *
     * @details
     * 通过 Q_PROPERTY 绑定向 QML 暴露注册表单状态（用户名、邮箱、密码、确认密码、
     * 加载中、错误消息、各字段验证错误）。用户输入时运行内联字段验证；
     * 最终的 submit() 调用触发 API 请求。所有业务逻辑都在 C++ 中。
     *
     * 单例边界审计（任务 7）：页面作用域（注册表单状态）。
     * 暂时保留 QML_SINGLETON 以保持类型注册和当前导入；
     * 计划迁移目标为显式页面级实例化。
     *
     * 单例生命周期：应用拥有的实例必须先通过 SetInstance() 创建并注册，
     * 然后 QML 引擎才能调用 create()。
     */
    class RegisterViewModel : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_SINGLETON

        // ==================== 属性 ====================

        Q_PROPERTY(QString username READ Username WRITE SetUsername NOTIFY usernameChanged) ///< 新账户的用户名（4-32 字符，字母数字+下划线）
        Q_PROPERTY(QString email READ Email WRITE SetEmail NOTIFY emailChanged) ///< 新账户的邮箱地址
        Q_PROPERTY(QString password READ Password WRITE SetPassword NOTIFY passwordChanged) ///< 新账户的密码（8-64 字符，大小写+数字）
        Q_PROPERTY(QString confirmPassword READ ConfirmPassword WRITE SetConfirmPassword NOTIFY confirmPasswordChanged) ///< 确认密码——提交前必须与密码匹配
        Q_PROPERTY(bool loading READ Loading NOTIFY loadingChanged) ///< 注册 API 请求进行中时为 true
        Q_PROPERTY(QString errorMessage READ ErrorMessage NOTIFY errorMessageChanged) ///< API 级别的注册失败错误消息；clearError() 清除
        Q_PROPERTY(bool canSubmit READ CanSubmit NOTIFY canSubmitChanged) ///< 所有字段有效、非空且无请求进行中时为 true
        Q_PROPERTY(QString usernameError READ UsernameError NOTIFY usernameErrorChanged) ///< 用户名字段的内联验证错误；有效或未触碰时为空
        Q_PROPERTY(QString emailError READ EmailError NOTIFY emailErrorChanged) ///< 邮箱字段的内联验证错误；有效或未触碰时为空
        Q_PROPERTY(QString passwordError READ PasswordError NOTIFY passwordErrorChanged) ///< 密码字段的内联验证错误；有效或未触碰时为空
        Q_PROPERTY(QString confirmPasswordError READ ConfirmPasswordError NOTIFY confirmPasswordErrorChanged) ///< 确认密码字段的内联验证错误；与密码匹配时为空
    public:
        explicit RegisterViewModel(services::AuthService* authService, QObject* parent = nullptr);

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

        [[nodiscard]] auto Username() const -> const QString&;
        [[nodiscard]] auto Email() const -> const QString&;
        [[nodiscard]] auto Password() const -> const QString&;
        [[nodiscard]] auto ConfirmPassword() const -> const QString&;
        [[nodiscard]] auto Loading() const -> bool;
        [[nodiscard]] auto ErrorMessage() const -> const QString&;
        [[nodiscard]] auto CanSubmit() const -> bool;
        [[nodiscard]] auto UsernameError() const -> const QString&;
        [[nodiscard]] auto EmailError() const -> const QString&;
        [[nodiscard]] auto PasswordError() const -> const QString&;
        [[nodiscard]] auto ConfirmPasswordError() const -> const QString&;

        auto SetUsername(const QString& username) -> void;
        auto SetEmail(const QString& email) -> void;
        auto SetPassword(const QString& password) -> void;
        auto SetConfirmPassword(const QString& confirmPassword) -> void;

        /**
         * @brief 提交注册表单。
         *
         * @details
         * 如果 canSubmit 为 false 或请求正在进行中则无操作。
         * 清除 errorMessage，设置 loading=true，然后调用 AuthService::Register()。
         * 成功时发射 registerSucceeded(username, email)。
         * 失败时设置 errorMessage 并清除 loading。
         *
         * 使用子上下文 QObject，以便在此 ViewModel 在响应到达前被销毁时
         * 自动丢弃回调。
         */

        /**
         * @brief 清除所有错误消息（全局和各字段）。
         *
         * @details
         * 将 errorMessage、usernameError、emailError、passwordError 和
         * confirmPasswordError 重置为空字符串。当用户导航离开
         * 并返回到干净的表单状态时很有用。
         */

        // ==================== Signals ====================

    signals:
        void usernameChanged();
        void emailChanged();
        void passwordChanged();
        void confirmPasswordChanged();
        void loadingChanged();
        void errorMessageChanged();
        void canSubmitChanged();
        void usernameErrorChanged();
        void emailErrorChanged();
        void passwordErrorChanged();
        void confirmPasswordErrorChanged();
        void registerSucceeded(const QString& username, const QString& email);

    private:
        // ==================== 私有辅助方法 ====================

        auto ValidateUsername() -> void;
        auto ValidateEmail() -> void;
        auto ValidatePassword() -> void;
        auto ValidateConfirmPassword() -> void;
        auto UpdateCanSubmit() -> void;
        auto SetLoading(bool loading) -> void;
        auto SetErrorMessage(const QString& message) -> void;
        auto SetUsernameError(const QString& error) -> void;
        auto SetEmailError(const QString& error) -> void;
        auto SetPasswordError(const QString& error) -> void;
        auto SetConfirmPasswordError(const QString& error) -> void;

        // ==================== 状态 ====================

        services::AuthService* m_auth_service; ///< 认证服务
        QString m_username; ///< 用户名
        QString m_email; ///< 邮箱
        QString m_password; ///< 密码
        QString m_confirm_password; ///< 确认密码
        bool m_loading{ false }; ///< 是否正在加载
        QString m_error_message; ///< 错误消息
        bool m_can_submit{ false }; ///< 是否可提交
        QString m_username_error; ///< 用户名错误
        QString m_email_error; ///< 邮箱错误
        QString m_password_error; ///< 密码错误
        QString m_confirm_password_error; ///< 确认密码错误

        inline static RegisterViewModel* s_instance = nullptr; ///< 单例实例
        inline static QJSEngine* s_engine = nullptr; ///< JS 引擎实例

} // namespace disk::qml::viewmodels
