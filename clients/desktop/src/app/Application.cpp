#include "Application.hpp"

#include <QQmlContext>

#include "ShellController.hpp"
#include "auth/AuthService.hpp"
#include "auth/OwnerSessionManager.hpp"
#include "auth/SessionStore.hpp"
#include "auth/VisitorSessionManager.hpp"
#include "managers/DriveManager.hpp"
#include "managers/ProfileManager.hpp"
#include "managers/ShareManager.hpp"
#include "managers/TransferManager.hpp"
#include "managers/TrashManager.hpp"
#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

namespace disk::app {

    Application::Application(QObject* parent)
        : QObject(parent), m_network_client(std::make_unique<disk::desktop::NetworkClient>()), m_request_factory(std::make_unique<disk::desktop::RequestFactory>()), m_auth_service(std::make_unique<disk::desktop::AuthService>(m_network_client.get(), m_request_factory.get())), m_session_store(std::make_unique<disk::desktop::SessionStore>(m_network_client.get(), m_request_factory.get())), m_drive_manager(std::make_unique<disk::desktop::managers::DriveManager>(m_network_client.get(), m_request_factory.get())), m_profile_manager(std::make_unique<disk::desktop::managers::ProfileManager>(m_network_client.get(), m_request_factory.get())), m_transfer_manager(std::make_unique<disk::desktop::managers::TransferManager>(m_network_client.get(), m_request_factory.get())), m_share_manager(std::make_unique<disk::desktop::managers::ShareManager>(m_network_client.get(), m_request_factory.get())), m_trash_manager(std::make_unique<disk::desktop::managers::TrashManager>(m_network_client.get(), m_request_factory.get())), m_shell_controller(std::make_unique<ShellController>(m_session_store.get())) {
    }

    Application::~Application() = default;

    void Application::Initialize(QQmlApplicationEngine* engine) {
        if (!engine) {
            return;
        }

        QQmlContext* context = engine->rootContext();

        context->setContextProperty("shellController", m_shell_controller.get());
        context->setContextProperty("authService", m_auth_service.get());
        context->setContextProperty("sessionStore", m_session_store.get());
        context->setContextProperty("driveManager", m_drive_manager.get());
        context->setContextProperty("profileManager", m_profile_manager.get());
        context->setContextProperty("transferManager", m_transfer_manager.get());
        context->setContextProperty("shareManager", m_share_manager.get());
        context->setContextProperty("trashManager", m_trash_manager.get());

        auto* owner_mgr = m_session_store->GetOwnerManager();

        // Login
        connect(m_auth_service.get(), &disk::desktop::AuthService::loginSuccess,
                owner_mgr, &disk::desktop::OwnerSessionManager::HandleLoginSuccess);
        connect(m_auth_service.get(), &disk::desktop::AuthService::loginFailure,
                owner_mgr, &disk::desktop::OwnerSessionManager::HandleLoginFailure);

        connect(owner_mgr, &disk::desktop::OwnerSessionManager::stateChanged,
                this, [this](disk::desktop::OwnerSessionState state) {
                    if (state == disk::desktop::OwnerSessionState::Active) {
                        m_session_store->ActivateOwner();
                        return;
                    }

                    if (state == disk::desktop::OwnerSessionState::LogoutPending ||
                        state == disk::desktop::OwnerSessionState::ReauthRequired) {
                        m_transfer_manager->ShutdownOwnerTransfers();
                    }
                });

        // Refresh
        connect(owner_mgr, &disk::desktop::OwnerSessionManager::refreshRequested,
                m_auth_service.get(), &disk::desktop::AuthService::RefreshToken);
        connect(m_auth_service.get(), &disk::desktop::AuthService::refreshSuccess,
                owner_mgr, &disk::desktop::OwnerSessionManager::HandleRefreshSuccess);
        connect(m_auth_service.get(), &disk::desktop::AuthService::refreshFailure,
                owner_mgr, &disk::desktop::OwnerSessionManager::HandleRefreshFailure);

        // Logout
        connect(owner_mgr, &disk::desktop::OwnerSessionManager::logoutRequested,
                m_auth_service.get(), &disk::desktop::AuthService::Logout);
        connect(m_auth_service.get(), &disk::desktop::AuthService::logoutSuccess,
                owner_mgr, &disk::desktop::OwnerSessionManager::CompleteLogout);
        connect(m_auth_service.get(), &disk::desktop::AuthService::logoutFailure,
                owner_mgr, &disk::desktop::OwnerSessionManager::CompleteLogout);

        auto* visitor_mgr = m_session_store->GetVisitorManager();

        connect(visitor_mgr, &disk::desktop::VisitorSessionManager::verifyRequested,
                m_auth_service.get(), &disk::desktop::AuthService::AccessShare);
        connect(visitor_mgr, &disk::desktop::VisitorSessionManager::reverifyRequested,
                m_auth_service.get(), &disk::desktop::AuthService::AccessShare);

        connect(m_auth_service.get(), &disk::desktop::AuthService::shareAccessSuccess,
                visitor_mgr, &disk::desktop::VisitorSessionManager::HandleVerifySuccess);
        connect(m_auth_service.get(), &disk::desktop::AuthService::shareAccessFailure,
                visitor_mgr, [visitor_mgr](int error_code, const QString&) {
                    visitor_mgr->HandleVerifyFailure(error_code);
                });

        m_shell_controller->Initialize();
    }

} // namespace disk::app
