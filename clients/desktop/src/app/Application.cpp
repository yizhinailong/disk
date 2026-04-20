#include "Application.hpp"

#include <QQmlContext>

#include "ShellController.hpp"
#include "auth/AuthService.hpp"
#include "auth/SessionStore.hpp"
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
        context->setContextProperty("sessionStore", m_session_store.get());
        context->setContextProperty("driveManager", m_drive_manager.get());
        context->setContextProperty("profileManager", m_profile_manager.get());
        context->setContextProperty("transferManager", m_transfer_manager.get());
        context->setContextProperty("shareManager", m_share_manager.get());
        context->setContextProperty("trashManager", m_trash_manager.get());

        m_shell_controller->Initialize();
    }

} // namespace disk::app
