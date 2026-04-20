/**
 * @file SessionStore.hpp
 * @brief Unified session state manager with Owner + Visitor state machines
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <QObject>
#include <QString>

#include "auth/OwnerSessionManager.hpp"
#include "auth/VisitorSessionManager.hpp"

namespace disk::desktop {

    class NetworkClient;
    class RequestFactory;

    /**
     * @brief Unified session state manager
     *
     * Owns OwnerSessionManager and VisitorSessionManager.
     * Enforces: only one active auth domain at a time.
     * Owner and Visitor tokens are NEVER mixed.
     */
    class SessionStore : public QObject {
        Q_OBJECT
        Q_PROPERTY(
            OwnerSessionManager* owner READ GetOwnerManager CONSTANT
        )
        Q_PROPERTY(
            VisitorSessionManager* visitor READ GetVisitorManager CONSTANT
        )
        Q_PROPERTY(
            QString activeDomain READ GetActiveDomain NOTIFY activeDomainChanged
        )

    public:
        explicit SessionStore(
            NetworkClient* network_client,
            RequestFactory* request_factory,
            QObject* parent = nullptr
        );

        auto GetOwnerManager() -> OwnerSessionManager*;
        auto GetVisitorManager() -> VisitorSessionManager*;
        auto GetActiveDomain() const -> QString;

    public slots:
        void ActivateOwner();
        void ActivateVisitor(const QString& share_id);
        void DeactivateAll();

    signals:
        void activeDomainChanged(const QString& domain);

    private:
        void SetActiveDomain(const QString& domain);

        NetworkClient* m_network_client;
        RequestFactory* m_request_factory;

        OwnerSessionManager* m_owner_manager;
        VisitorSessionManager* m_visitor_manager;

        QString m_active_domain;
    };

} // namespace disk::desktop
