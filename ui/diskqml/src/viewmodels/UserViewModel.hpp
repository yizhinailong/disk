/**
 * @file UserViewModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QML singleton ViewModel for user profile, storage, and password workflows
 * @version 0.1
 * @date 2026-03-10
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
     * @brief QML singleton ViewModel for user profile, storage, and password management.
     *
     * @details
     * Exposes user profile data, storage statistics, and password change functionality
     * to QML via Q_PROPERTY bindings. All business logic is handled in C++ via UserService;
     * QML only drives the UI.
     *
     * Password validation is performed locally before making API calls:
     * - New password minimum length: 8 characters
     * - New password and confirm password must match
     *
     * Singleton lifecycle: an application-owned instance must be created and
     * registered with SetInstance() before the QML engine calls create().
     */
    class UserViewModel : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_SINGLETON

        // ==================== Profile Properties (Read-Only from API) ====================

        /// User's login name (read-only).
        Q_PROPERTY(QString username READ Username NOTIFY profileChanged)
        /// User's email address (read-only).
        Q_PROPERTY(QString email READ Email NOTIFY profileChanged)
        /// User's display nickname.
        Q_PROPERTY(QString nickname READ Nickname NOTIFY profileChanged)
        /// User's avatar URL.
        Q_PROPERTY(QString avatar READ Avatar NOTIFY profileChanged)

        // ==================== Storage Properties (Read-Only from API) ====================

        /// Bytes used.
        Q_PROPERTY(quint64 storageUsed READ StorageUsed NOTIFY storageChanged)
        /// Bytes quota.
        Q_PROPERTY(quint64 storageQuota READ StorageQuota NOTIFY storageChanged)
        /// Usage percentage (0-100).
        Q_PROPERTY(double storagePercentage READ StoragePercentage NOTIFY storageChanged)
        /// Total file count.
        Q_PROPERTY(int fileCount READ FileCount NOTIFY storageChanged)
        /// Total folder count.
        Q_PROPERTY(int folderCount READ FolderCount NOTIFY storageChanged)

        // ==================== Profile Edit Form ====================

        /// Editable nickname for updateProfile().
        Q_PROPERTY(QString editNickname READ EditNickname WRITE SetEditNickname NOTIFY editNicknameChanged)
        /// Editable avatar URL for updateProfile().
        Q_PROPERTY(QString editAvatar READ EditAvatar WRITE SetEditAvatar NOTIFY editAvatarChanged)

        // ==================== Password Change Form ====================

        /// Current password input for changePassword().
        Q_PROPERTY(QString oldPassword READ OldPassword WRITE SetOldPassword NOTIFY oldPasswordChanged)
        /// New password input for changePassword().
        Q_PROPERTY(QString newPassword READ NewPassword WRITE SetNewPassword NOTIFY newPasswordChanged)
        /// Confirm password input for changePassword().
        Q_PROPERTY(QString confirmPassword READ ConfirmPassword WRITE SetConfirmPassword NOTIFY confirmPasswordChanged)
        /// Validation error for password form (empty if valid).
        Q_PROPERTY(QString passwordError READ PasswordError NOTIFY passwordErrorChanged)
        /// True when password form is valid and can be submitted.
        Q_PROPERTY(bool canChangePassword READ CanChangePassword NOTIFY canChangePasswordChanged)

        // ==================== State ====================

        /// True while any API call is in flight.
        Q_PROPERTY(bool loading READ Loading NOTIFY loadingChanged)
        /// Non-empty when an operation failed.
        Q_PROPERTY(QString errorMessage READ ErrorMessage NOTIFY errorMessageChanged)

    public:
        explicit UserViewModel(services::UserService* userService, QObject* parent = nullptr);

        // ==================== Singleton API ====================

        /**
         * @brief Register the pre-created instance for use by the QML engine.
         */
        static auto SetInstance(UserViewModel* instance) -> void;

        /**
         * @brief QML singleton factory — called once by the QML engine.
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
        [[nodiscard]] auto CanChangePassword() const -> bool;

        auto SetOldPassword(const QString& value) -> void;
        auto SetNewPassword(const QString& value) -> void;
        auto SetConfirmPassword(const QString& value) -> void;

        // ==================== State Getters ====================

        [[nodiscard]] auto Loading() const -> bool;
        [[nodiscard]] auto ErrorMessage() const -> const QString&;

        // ==================== Actions ====================

        /**
         * @brief Fetch user profile from the server.
         *
         * @details
         * Calls UserService::GetProfile(). Updates profile properties on success.
         * Also updates editNickname and editAvatar to current values.
         */
        Q_INVOKABLE void loadProfile();

        /**
         * @brief Fetch storage statistics from the server.
         *
         * @details
         * Calls UserService::GetStorage(). Updates storage properties on success.
         */
        Q_INVOKABLE void loadStorage();

        /**
         * @brief Update user profile with edited nickname/avatar.
         *
         * @details
         * Calls UserService::UpdateProfile() with editNickname and editAvatar.
         * At least one field must be non-empty. Updates profile properties on success.
         */
        Q_INVOKABLE void updateProfile();

        /**
         * @brief Change user password.
         *
         * @details
         * Validates password form locally before calling UserService::ChangePassword():
         * - newPassword length >= 8
         * - newPassword == confirmPassword
         * - oldPassword non-empty
         * If validation fails, sets passwordError and returns without API call.
         * On success, clears password form fields.
         */
        Q_INVOKABLE void changePassword();

        /**
         * @brief Clear the current error message.
         */
        Q_INVOKABLE void clearError();

        /**
         * @brief Reset password form to empty state.
         */
        Q_INVOKABLE void clearPasswordForm();

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
        void loadingChanged();
        void errorMessageChanged();

        /// Emitted when profile update succeeds.
        void profileUpdated();
        /// Emitted when password change succeeds.
        void passwordChanged();

    private:
        // ==================== Private Helpers ====================

        auto SetLoading(bool loading) -> void;
        auto SetErrorMessage(const QString& message) -> void;
        auto SetPasswordError(const QString& error) -> void;
        auto ValidatePasswordForm() -> bool;
        auto UpdateCanChangePassword() -> void;

        // ==================== Dependencies ====================

        services::UserService* m_user_service;

        // ==================== Profile State ====================

        QString m_username;
        QString m_email;
        QString m_nickname;
        QString m_avatar;

        // ==================== Storage State ====================

        quint64 m_storage_used{ 0 };
        quint64 m_storage_quota{ 0 };
        double m_storage_percentage{ 0.0 };
        int m_file_count{ 0 };
        int m_folder_count{ 0 };

        // ==================== Edit Form State ====================

        QString m_edit_nickname;
        QString m_edit_avatar;

        // ==================== Password Form State ====================

        QString m_old_password;
        QString m_new_password;
        QString m_confirm_password;
        QString m_password_error;
        bool m_can_change_password{ false };

        // ==================== Loading/Error State ====================

        bool m_loading{ false };
        QString m_error_message;

        // ==================== Singleton Instance ====================

        inline static UserViewModel* s_instance = nullptr;
        inline static QJSEngine* s_engine = nullptr;
    };

} // namespace disk::qml::viewmodels
