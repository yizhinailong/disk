/**
 * @file SessionViewModel.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief SessionViewModel implementation
 * @version 0.1
 * @date 2026-03-02
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "SessionViewModel.hpp"

#include <QJSEngine>
#include <QQmlEngine>

#include <services/AuthService.hpp>
#include <services/TokenStore.hpp>
#include <utils/ConfigStore.hpp>
#include <viewmodels/LoginViewModel.hpp>

namespace disk::qml::viewmodels {

    SessionViewModel::SessionViewModel(
        LoginViewModel* loginViewModel,
        services::TokenStore* tokenStore,
        services::AuthService* authService,
        utils::ConfigStore* configStore,
        QObject* parent
    ) : QObject(parent),
        m_login_view_model(loginViewModel),
        m_token_store(tokenStore),
        m_auth_service(authService),
        m_server_url(configStore->ServerUrl().toString()) {
        // Initialize logged-in state from existing token
        m_is_logged_in = m_token_store->HasValidAccessToken();

        // Connect login success → update session state including storage metrics
        connect(
            m_login_view_model,
            &LoginViewModel::loginSucceeded,
            this,
            [this](const QString& username, quint64 storageUsed, quint64 storageQuota) {
                SetLoggedInUserName(username);
                SetStorageUsed(static_cast<qint64>(storageUsed));
                SetStorageQuota(static_cast<qint64>(storageQuota));
                UpdateIsLoggedIn();
            }
        );
    }

    auto SessionViewModel::IsLoggedIn() const -> bool {
        return m_is_logged_in;
    }

    auto SessionViewModel::LoggedInUserName() const -> const QString& {
        return m_logged_in_user_name;
    }

    auto SessionViewModel::StorageUsed() const -> qint64 {
        return m_storage_used;
    }

    auto SessionViewModel::StorageQuota() const -> qint64 {
        return m_storage_quota;
    }

    auto SessionViewModel::StorageUsedFormatted() const -> QString {
        return FormatBytes(m_storage_used);
    }

    auto SessionViewModel::StorageQuotaFormatted() const -> QString {
        return FormatBytes(m_storage_quota);
    }

    auto SessionViewModel::StoragePercentage() const -> double {
        if (m_storage_quota <= 0) {
            return 0.0;
        }
        return static_cast<double>(m_storage_used) / static_cast<double>(m_storage_quota) * 100.0;
    }

    auto SessionViewModel::ServerUrl() const -> const QString& {
        return m_server_url;
    }

    void SessionViewModel::logout() {
        const QString accessToken = m_token_store->AccessToken();

        // Context object prevents callback invocation if SessionViewModel is destroyed
        auto* ctx = new QObject(this);

        m_auth_service->Logout(
            accessToken,
            ctx,
            [this, ctx](bool /*ok*/, QString /*errorMessage*/) {
                ctx->deleteLater();

                // Clear local state regardless of server response
                m_token_store->Clear();
                SetLoggedInUserName(QString{});
                SetStorageUsed(0);
                SetStorageQuota(0);
                UpdateIsLoggedIn();
            }
        );
    }

    auto SessionViewModel::SetLoggedInUserName(const QString& name) -> void {
        if (m_logged_in_user_name == name) {
            return;
        }
        m_logged_in_user_name = name;
        emit loggedInUserNameChanged();
    }

    auto SessionViewModel::UpdateIsLoggedIn() -> void {
        const bool loggedIn = m_token_store->HasValidAccessToken();
        if (m_is_logged_in == loggedIn) {
            return;
        }
        m_is_logged_in = loggedIn;
        emit isLoggedInChanged();
    }

    auto SessionViewModel::SetStorageUsed(qint64 bytes) -> void {
        if (m_storage_used == bytes) {
            return;
        }
        m_storage_used = bytes;
        emit storageUsedChanged();
        emit storagePercentageChanged();
    }

    auto SessionViewModel::SetStorageQuota(qint64 bytes) -> void {
        if (m_storage_quota == bytes) {
            return;
        }
        m_storage_quota = bytes;
        emit storageQuotaChanged();
        emit storagePercentageChanged();
    }

    auto SessionViewModel::FormatBytes(qint64 bytes) -> QString {
        if (bytes <= 0) {
            return QStringLiteral("0 B");
        }

        constexpr double kKB = 1024.0;
        constexpr double kMB = kKB * 1024.0;
        constexpr double kGB = kMB * 1024.0;
        constexpr double kTB = kGB * 1024.0;

        const auto value = static_cast<double>(bytes);

        if (value >= kTB) {
            return QString::number(value / kTB, 'f', 2) + QStringLiteral(" TB");
        }
        if (value >= kGB) {
            return QString::number(value / kGB, 'f', 2) + QStringLiteral(" GB");
        }
        if (value >= kMB) {
            return QString::number(value / kMB, 'f', 2) + QStringLiteral(" MB");
        }
        if (value >= kKB) {
            return QString::number(value / kKB, 'f', 2) + QStringLiteral(" KB");
        }
        return QString::number(bytes) + QStringLiteral(" B");
    }

    auto SessionViewModel::SetInstance(SessionViewModel* instance) -> void {
        s_instance = instance;
    }

    auto SessionViewModel::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> SessionViewModel* {
        // The instance has to exist before it is used. We cannot replace it.
        Q_ASSERT(s_instance != nullptr);

        // The engine has to have the same thread affinity as the singleton.
        Q_ASSERT(qmlEngine->thread() == s_instance->thread());

        // There can only be one engine accessing the singleton.
        if (s_engine) {
            Q_ASSERT(jsEngine == s_engine);
        } else {
            s_engine = jsEngine;
        }

        // Explicitly specify C++ ownership so that the engine doesn't delete the instance.
        QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
        return s_instance;
    }

} // namespace disk::qml::viewmodels
