#pragma once

#include <QObject>
#include <QQmlApplicationEngine>
#include <memory>

namespace disk::desktop {
    class NetworkClient;
    class RequestFactory;
    class AuthService;
    class SessionStore;
} // namespace disk::desktop

namespace disk::desktop::managers {
    class AdminManager;
    class DriveManager;
    class HealthManager;
    class NetworkSettingsManager;
    class ProfileManager;
    class TransferManager;
    class ShareManager;
    class TrashManager;
} // namespace disk::desktop::managers

namespace disk::app {

    class ShellController;

    class Application : public QObject {
        Q_OBJECT

    public:
        explicit Application(QObject* parent = nullptr);
        ~Application() override;

        void Initialize(QQmlApplicationEngine* engine);

    private:
        std::unique_ptr<disk::desktop::NetworkClient> m_network_client;
        std::unique_ptr<disk::desktop::RequestFactory> m_request_factory;
        std::unique_ptr<disk::desktop::AuthService> m_auth_service;
        std::unique_ptr<disk::desktop::SessionStore> m_session_store;

        std::unique_ptr<disk::desktop::managers::AdminManager> m_admin_manager;
        std::unique_ptr<disk::desktop::managers::DriveManager> m_drive_manager;
        std::unique_ptr<disk::desktop::managers::HealthManager> m_health_manager;
        std::unique_ptr<disk::desktop::managers::NetworkSettingsManager> m_network_settings_manager;
        std::unique_ptr<disk::desktop::managers::ProfileManager> m_profile_manager;
        std::unique_ptr<disk::desktop::managers::TransferManager> m_transfer_manager;
        std::unique_ptr<disk::desktop::managers::ShareManager> m_share_manager;
        std::unique_ptr<disk::desktop::managers::TrashManager> m_trash_manager;

        std::unique_ptr<ShellController> m_shell_controller;
    };

} // namespace disk::app
