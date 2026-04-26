/**
 * @file SessionStore.cpp
 * @brief Unified session state manager implementation
 *
 * @copyright Copyright (c) 2026
 */

#include "auth/SessionStore.hpp"

#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

namespace disk::desktop {

    SessionStore::SessionStore(
        NetworkClient* network_client,
        RequestFactory* request_factory,
        QObject* parent
    )
        : QObject(parent), m_network_client(network_client), m_request_factory(request_factory) {
        m_owner_manager = new OwnerSessionManager(
            network_client,
            request_factory,
            this
        );
        m_visitor_manager = new VisitorSessionManager(
            network_client,
            request_factory,
            this
        );
    }

    auto SessionStore::GetOwnerManager() -> OwnerSessionManager* {
        return m_owner_manager;
    }

    auto SessionStore::GetVisitorManager() -> VisitorSessionManager* {
        return m_visitor_manager;
    }

    auto SessionStore::GetActiveDomain() const -> QString {
        return m_active_domain;
    }

    void SessionStore::ActivateOwner() {
        if (m_active_domain == "visitor") {
            m_visitor_manager->CloseShare();
        }
        SetActiveDomain("owner");
    }

    void SessionStore::ActivateVisitor(const QString& share_id) {
        SetActiveDomain("visitor");
        m_visitor_manager->OpenShare(share_id);
    }

    void SessionStore::DeactivateAll() {
        m_owner_manager->StartLogout();
        m_visitor_manager->CloseShare();
        SetActiveDomain("");
    }

    void SessionStore::SetActiveDomain(const QString& domain) {
        if (m_active_domain != domain) {
            m_active_domain = domain;
            emit activeDomainChanged(m_active_domain);
        }
    }

} // namespace disk::desktop
