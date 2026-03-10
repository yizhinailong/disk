/**
 * @file UserViewModel.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief UserViewModel implementation
 * @version 0.1
 * @date 2026-03-10
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "UserViewModel.hpp"

#include <QJSEngine>
#include <QQmlEngine>

#include <services/UserService.hpp>

namespace disk::qml::viewmodels {

    UserViewModel::UserViewModel(services::UserService* userService, QObject* parent)
        : QObject(parent), m_user_service(userService) {
    }

    // ==================== Singleton API ====================

    auto UserViewModel::SetInstance(UserViewModel* instance) -> void {
        s_instance = instance;
    }

    auto UserViewModel::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> UserViewModel* {
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

    // ==================== Profile Getters ====================

    auto UserViewModel::Username() const -> const QString& {
        return m_username;
    }

    auto UserViewModel::Email() const -> const QString& {
        return m_email;
    }

    auto UserViewModel::Nickname() const -> const QString& {
        return m_nickname;
    }

    auto UserViewModel::Avatar() const -> const QString& {
        return m_avatar;
    }

    // ==================== Storage Getters ====================

    auto UserViewModel::StorageUsed() const -> quint64 {
        return m_storage_used;
    }

    auto UserViewModel::StorageQuota() const -> quint64 {
        return m_storage_quota;
    }

    auto UserViewModel::StoragePercentage() const -> double {
        return m_storage_percentage;
    }

    auto UserViewModel::FileCount() const -> int {
        return m_file_count;
    }

    auto UserViewModel::FolderCount() const -> int {
        return m_folder_count;
    }

    // ==================== Edit Form Getters/Setters ====================

    auto UserViewModel::EditNickname() const -> const QString& {
        return m_edit_nickname;
    }

    auto UserViewModel::EditAvatar() const -> const QString& {
        return m_edit_avatar;
    }

    auto UserViewModel::SetEditNickname(const QString& value) -> void {
        if (m_edit_nickname == value) {
            return;
        }
        m_edit_nickname = value;
        emit editNicknameChanged();
    }

    auto UserViewModel::SetEditAvatar(const QString& value) -> void {
        if (m_edit_avatar == value) {
            return;
        }
        m_edit_avatar = value;
        emit editAvatarChanged();
    }

    // ==================== Password Form Getters/Setters ====================

    auto UserViewModel::OldPassword() const -> const QString& {
        return m_old_password;
    }

    auto UserViewModel::NewPassword() const -> const QString& {
        return m_new_password;
    }

    auto UserViewModel::ConfirmPassword() const -> const QString& {
        return m_confirm_password;
    }

    auto UserViewModel::PasswordError() const -> const QString& {
        return m_password_error;
    }

    auto UserViewModel::CanChangePassword() const -> bool {
        return m_can_change_password;
    }

    auto UserViewModel::SetOldPassword(const QString& value) -> void {
        if (m_old_password == value) {
            return;
        }
        m_old_password = value;
        emit oldPasswordChanged();
        UpdateCanChangePassword();
    }

    auto UserViewModel::SetNewPassword(const QString& value) -> void {
        if (m_new_password == value) {
            return;
        }
        m_new_password = value;
        emit newPasswordChanged();
        UpdateCanChangePassword();
    }

    auto UserViewModel::SetConfirmPassword(const QString& value) -> void {
        if (m_confirm_password == value) {
            return;
        }
        m_confirm_password = value;
        emit confirmPasswordChanged();
        UpdateCanChangePassword();
    }

    // ==================== State Getters ====================

    auto UserViewModel::Loading() const -> bool {
        return m_loading;
    }

    auto UserViewModel::ErrorMessage() const -> const QString& {
        return m_error_message;
    }

    // ==================== Actions ====================

    void UserViewModel::loadProfile() {
        if (m_loading) {
            return;
        }

        SetLoading(true);
        SetErrorMessage(QString{});

        auto* ctx = new QObject(this);

        m_user_service->GetProfile(
            ctx,
            [this, ctx](std::optional<models::UserProfileDto> result, QString errorMessage) {
                ctx->deleteLater();
                SetLoading(false);

                if (!result) {
                    SetErrorMessage(errorMessage);
                    return;
                }

                m_username = result->username;
                m_email = result->email;
                m_nickname = result->nickname;
                m_avatar = result->avatar;
                m_edit_nickname = result->nickname;
                m_edit_avatar = result->avatar;

                emit profileChanged();
                emit editNicknameChanged();
                emit editAvatarChanged();
            }
        );
    }

    void UserViewModel::loadStorage() {
        if (m_loading) {
            return;
        }

        SetLoading(true);
        SetErrorMessage(QString{});

        auto* ctx = new QObject(this);

        m_user_service->GetStorage(
            ctx,
            [this, ctx](std::optional<models::StorageDto> result, QString errorMessage) {
                ctx->deleteLater();
                SetLoading(false);

                if (!result) {
                    SetErrorMessage(errorMessage);
                    return;
                }

                m_storage_used = result->used;
                m_storage_quota = result->quota;
                m_storage_percentage = result->percentage;
                m_file_count = result->fileCount;
                m_folder_count = result->folderCount;

                emit storageChanged();
            }
        );
    }

    void UserViewModel::updateProfile() {
        if (m_loading) {
            return;
        }

        const QString nickname = m_edit_nickname.trimmed();
        const QString avatar = m_edit_avatar.trimmed();

        if (nickname.isEmpty() && avatar.isEmpty()) {
            SetErrorMessage(tr("At least one of nickname or avatar must be provided"));
            return;
        }

        SetLoading(true);
        SetErrorMessage(QString{});

        auto* ctx = new QObject(this);

        m_user_service->UpdateProfile(
            nickname,
            avatar,
            ctx,
            [this, ctx, nickname, avatar](std::optional<models::UpdateProfileResultDto> result, QString errorMessage) {
                ctx->deleteLater();
                SetLoading(false);

                if (!result) {
                    SetErrorMessage(errorMessage);
                    return;
                }

                m_nickname = result->nickname.isEmpty() ? nickname : result->nickname;
                m_avatar = result->avatar.isEmpty() ? avatar : result->avatar;

                emit profileChanged();
                emit profileUpdated();
            }
        );
    }

    void UserViewModel::changePassword() {
        if (!m_can_change_password || m_loading) {
            return;
        }

        if (!ValidatePasswordForm()) {
            return;
        }

        SetLoading(true);
        SetErrorMessage(QString{});
        SetPasswordError(QString{});

        auto* ctx = new QObject(this);

        m_user_service->ChangePassword(
            m_old_password,
            m_new_password,
            ctx,
            [this, ctx](std::optional<models::ChangePasswordResultDto> result, QString errorMessage) {
                ctx->deleteLater();
                SetLoading(false);

                if (!result) {
                    SetErrorMessage(errorMessage);
                    return;
                }

                m_old_password.clear();
                m_new_password.clear();
                m_confirm_password.clear();

                emit oldPasswordChanged();
                emit newPasswordChanged();
                emit confirmPasswordChanged();
                UpdateCanChangePassword();
                emit passwordChanged();
            }
        );
    }

    void UserViewModel::clearError() {
        SetErrorMessage(QString{});
    }

    void UserViewModel::clearPasswordForm() {
        m_old_password.clear();
        m_new_password.clear();
        m_confirm_password.clear();
        m_password_error.clear();
        m_can_change_password = false;

        emit oldPasswordChanged();
        emit newPasswordChanged();
        emit confirmPasswordChanged();
        emit passwordErrorChanged();
        emit canChangePasswordChanged();
    }

    // ==================== Private Helpers ====================

    auto UserViewModel::SetLoading(bool loading) -> void {
        if (m_loading == loading) {
            return;
        }
        m_loading = loading;
        emit loadingChanged();
    }

    auto UserViewModel::SetErrorMessage(const QString& message) -> void {
        if (m_error_message == message) {
            return;
        }
        m_error_message = message;
        emit errorMessageChanged();
    }

    auto UserViewModel::SetPasswordError(const QString& error) -> void {
        if (m_password_error == error) {
            return;
        }
        m_password_error = error;
        emit passwordErrorChanged();
    }

    auto UserViewModel::ValidatePasswordForm() -> bool {
        if (m_old_password.isEmpty()) {
            SetPasswordError(tr("Current password is required"));
            return false;
        }

        if (m_new_password.length() < 8) {
            SetPasswordError(tr("New password must be at least 8 characters"));
            return false;
        }

        if (m_new_password != m_confirm_password) {
            SetPasswordError(tr("New password and confirm password do not match"));
            return false;
        }

        SetPasswordError(QString{});
        return true;
    }

    auto UserViewModel::UpdateCanChangePassword() -> void {
        const bool canChange =
            !m_old_password.isEmpty() &&
            m_new_password.length() >= 8 &&
            !m_confirm_password.isEmpty() &&
            !m_loading;

        if (m_can_change_password == canChange) {
            return;
        }
        m_can_change_password = canChange;
        emit canChangePasswordChanged();
    }

} // namespace disk::qml::viewmodels
