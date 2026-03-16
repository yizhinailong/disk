/**
 * @file UserViewModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 用户视图模型，管理用户资料、存储和密码
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
    class UserService;
}

namespace disk::qml::viewmodels {

    /**
     * @brief 用户资料、存储和密码管理的 QML 单例视图模型。
     *
     * @details
     * 通过 Q_PROPERTY 绑定向 QML 暴露用户资料数据、存储统计和密码更改功能。
     * 所有业务逻辑通过 UserService 在 C++ 中处理；QML 仅驱动 UI。
     *
     * 密码验证在调用 API 前本地进行：
     * - 新密码最小长度：8 字符
     * - 新密码和确认密码必须匹配
     *
     * 单例边界审计（任务 7）：页面作用域（资料/设置页面工作流状态）。
     * 暂时保留 QML_SINGLETON 以保持类型注册和导入；
     * 计划迁移目标为显式实例化/注入。
     *
     * 单例生命周期：应用拥有的实例必须先通过 SetInstance() 创建并注册，
     * 然后 QML 引擎才能调用 create()。
     */
    class UserViewModel : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_SINGLETON

        // ==================== 资料属性（从 API 只读） ====================

        Q_PROPERTY(QString username READ Username NOTIFY profileChanged) ///< 用户登录名（只读）
        Q_PROPERTY(QString email READ Email NOTIFY profileChanged)       ///< 用户邮箱地址（只读）
        Q_PROPERTY(QString nickname READ Nickname NOTIFY profileChanged) ///< 用户显示昵称
        Q_PROPERTY(QString avatar READ Avatar NOTIFY profileChanged)     ///< 用户头像 URL

        // ==================== 存储属性（从 API 只读） ====================

        Q_PROPERTY(quint64 storageUsed READ StorageUsed NOTIFY storageChanged)            ///< 已用字节
        Q_PROPERTY(quint64 storageQuota READ StorageQuota NOTIFY storageChanged)          ///< 配额字节
        Q_PROPERTY(double storagePercentage READ StoragePercentage NOTIFY storageChanged) ///< 使用百分比（0-100）
        Q_PROPERTY(int fileCount READ FileCount NOTIFY storageChanged)                    ///< 总文件数
        Q_PROPERTY(int folderCount READ FolderCount NOTIFY storageChanged)                ///< 总文件夹数

        // ==================== 资料编辑表单 ====================

        Q_PROPERTY(QString editNickname READ EditNickname WRITE SetEditNickname NOTIFY editNicknameChanged) ///< 可编辑的昵称（用于 updateProfile）
        Q_PROPERTY(QString editAvatar READ EditAvatar WRITE SetEditAvatar NOTIFY editAvatarChanged)         ///< 可编辑的头像 URL（用于 updateProfile）

        // ==================== 密码更改表单 ====================

        Q_PROPERTY(QString oldPassword READ OldPassword WRITE SetOldPassword NOTIFY oldPasswordChanged)                 ///< 当前密码输入（用于 changePassword）
        Q_PROPERTY(QString newPassword READ NewPassword WRITE SetNewPassword NOTIFY newPasswordChanged)                 ///< 新密码输入（用于 changePassword）
        Q_PROPERTY(QString confirmPassword READ ConfirmPassword WRITE SetConfirmPassword NOTIFY confirmPasswordChanged) ///< 确认密码输入（用于 changePassword）
        Q_PROPERTY(QString passwordError READ PasswordError NOTIFY passwordErrorChanged)                                ///< 密码表单验证错误（有效时为空）
        Q_PROPERTY(bool canChangePassword READ CanChangePassword NOTIFY canChangePasswordChanged)                       ///< 密码表单有效且可提交时为 true
        Q_PROPERTY(QString passwordStrengthMessage READ PasswordStrengthMessage NOTIFY passwordStrengthMessageChanged)  ///< 密码强度验证消息（用于实时 UI 反馈）
        Q_PROPERTY(bool isPasswordValid READ IsPasswordValid NOTIFY passwordStrengthMessageChanged)                     ///< 密码通过所有验证规则时为 true

        // ==================== 状态 ====================

        Q_PROPERTY(bool loading READ Loading NOTIFY loadingChanged)                   ///< 任何 API 请求进行中时为 true
        Q_PROPERTY(QString errorMessage READ ErrorMessage NOTIFY errorMessageChanged) ///< 操作失败时的错误消息

    public:
        explicit UserViewModel(services::UserService* userService, QObject* parent = nullptr);

        // ==================== Singleton API ====================

        /**
         * @brief 注册预创建的实例供 QML 引擎使用。
         */
        static auto SetInstance(UserViewModel* instance) -> void;

        /**
         * @brief QML 单例工厂——由 QML 引擎调用一次。
         */
        static auto create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> UserViewModel*;

        // ==================== Profile Getters ====================

        [[nodiscard]] auto Username() const -> const QString&;
        [[nodiscard]] auto Email() const -> const QString&;
        [[nodiscard]] auto Nickname() const -> const QString&;
        [[nodiscard]] auto Avatar() const -> const QString&;

        // ==================== Storage Getters ====================

        [[nodiscard]] auto StorageUsed() const -> quint64;
        [[nodiscard]] auto StorageQuota() const -> quint64;
        [[nodiscard]] auto StoragePercentage() const -> double;
        [[nodiscard]] auto FileCount() const -> int;
        [[nodiscard]] auto FolderCount() const -> int;

        // ==================== Edit Form Getters/Setters ====================

        [[nodiscard]] auto EditNickname() const -> const QString&;
        [[nodiscard]] auto EditAvatar() const -> const QString&;
        auto SetEditNickname(const QString& value) -> void;
        auto SetEditAvatar(const QString& value) -> void;

        // ==================== Password Form Getters/Setters ====================

        [[nodiscard]] auto OldPassword() const -> const QString&;
        [[nodiscard]] auto NewPassword() const -> const QString&;
        [[nodiscard]] auto ConfirmPassword() const -> const QString&;
        [[nodiscard]] auto PasswordError() const -> const QString&;
        [[nodiscard]] auto PasswordStrengthMessage() const -> const QString&;
        [[nodiscard]] auto IsPasswordValid() const -> bool;
        [[nodiscard]] auto CanChangePassword() const -> bool;

        auto SetOldPassword(const QString& value) -> void;
        auto SetNewPassword(const QString& value) -> void;
        auto SetConfirmPassword(const QString& value) -> void;

        // ==================== State Getters ====================

        [[nodiscard]] auto Loading() const -> bool;
        [[nodiscard]] auto ErrorMessage() const -> const QString&;

        // ==================== Actions ====================

        /**
         * @brief 从服务器获取用户资料。
         *
         * @details
         * 调用 UserService::GetProfile()。成功时更新资料属性。
         * 同时将 editNickname 和 editAvatar 更新为当前值。
         */
        Q_INVOKABLE void loadProfile();

        /**
         * @brief 从服务器获取存储统计。
         *
         * @details
         * 调用 UserService::GetStorage()。成功时更新存储属性。
         */
        Q_INVOKABLE void loadStorage();

        /**
         * @brief 使用编辑的昵称/头像更新用户资料。
         *
         * @details
         * 使用 editNickname 和 editAvatar 调用 UserService::UpdateProfile()。
         * 至少一个字段必须非空。成功时更新资料属性。
         */
        Q_INVOKABLE void updateProfile();

        /**
         * @brief 更改用户密码。
         *
         * @details
         * 在调用 UserService::ChangePassword() 前本地验证密码表单：
         * - newPassword 长度 >= 8
         * - newPassword == confirmPassword
         * - oldPassword 非空
         * 如果验证失败，设置 passwordError 并返回，不调用 API。
         * 成功时清空密码表单字段。
         */
        Q_INVOKABLE void changePassword();

        /**
         * @brief 清除当前错误消息。
         */
        Q_INVOKABLE void clearError();

        /**
         * @brief 将密码表单重置为空状态。
         */
        Q_INVOKABLE void clearPasswordForm();
        /**
         * @brief 验证密码强度并更新 passwordStrengthMessage。
         *
         * @details
         * 检查密码是否符合规则：
         * - 长度 8-64 字符
         * - 至少包含一个小写字母
         * - 至少包含一个大写字母
         * - 至少包含一个数字
         *
         * 更新 passwordStrengthMessage 属性为验证结果。
         * 当 newPassword 变化时从 QML 调用此方法。
         */
        Q_INVOKABLE void validatePasswordStrength(const QString& password);

        // ==================== Signals ====================

    signals:
        void profileChanged();
        void storageChanged();
        void editNicknameChanged();
        void editAvatarChanged();
        void oldPasswordChanged();
        void newPasswordChanged();
        void confirmPasswordChanged();
        void passwordErrorChanged();
        void canChangePasswordChanged();
        void passwordStrengthMessageChanged();
        void loadingChanged();
        void errorMessageChanged();

        /// 资料更新成功时发射
        void profileUpdated();
        /// 密码更改成功时发射
        void passwordChanged();

    private:
        // ==================== 私有辅助方法 ====================

        auto SetLoading(bool loading) -> void;
        auto SetErrorMessage(const QString& message) -> void;
        auto SetPasswordError(const QString& error) -> void;
        auto ValidatePasswordForm() -> bool;
        auto UpdateCanChangePassword() -> void;

        // ==================== 依赖 ====================

        services::UserService* m_user_service; ///< 用户服务

        // ==================== 资料状态 ====================

        QString m_username; ///< 用户名
        QString m_email;    ///< 邮箱
        QString m_nickname; ///< 昵称
        QString m_avatar;   ///< 头像

        // ==================== 存储状态 ====================

        quint64 m_storage_used{ 0 };        ///< 已用存储
        quint64 m_storage_quota{ 0 };       ///< 存储配额
        double m_storage_percentage{ 0.0 }; ///< 存储百分比
        int m_file_count{ 0 };              ///< 文件数
        int m_folder_count{ 0 };            ///< 文件夹数

        // ==================== 编辑表单状态 ====================

        QString m_edit_nickname; ///< 编辑昵称
        QString m_edit_avatar;   ///< 编辑头像

        // ==================== 密码表单状态 ====================

        QString m_old_password;              ///< 旧密码
        QString m_new_password;              ///< 新密码
        QString m_confirm_password;          ///< 确认密码
        QString m_password_error;            ///< 密码错误
        QString m_password_strength_message; ///< 密码强度消息
        bool m_is_password_valid{ false };   ///< 密码是否有效
        bool m_can_change_password{ false }; ///< 是否可更改密码

        // ==================== 加载/错误状态 ====================

        bool m_loading{ false }; ///< 是否正在加载
        QString m_error_message; ///< 错误消息

        // ==================== 单例实例 ====================

        inline static UserViewModel* s_instance = nullptr; ///< 单例实例
        inline static QJSEngine* s_engine = nullptr;       ///< JS 引擎实例
    };

} // namespace disk::qml::viewmodels
