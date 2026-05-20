/**
 * @file RequestFactory.hpp
 * @brief Auth-domain-aware request factory
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <QMap>
#include <QString>

namespace disk::desktop {

    enum class AuthDomain { Public,
                            Owner,
                            Visitor,
                            OwnerAndVisitor };

    /**
     * @brief Creates pre-configured request headers based on auth domain
     *
     * Owner domain: injects Authorization: Bearer <access_token>
     * Visitor domain: injects X-Share-Token: <share_token>
     * OwnerAndVisitor domain: injects both headers for save-to-drive
     * Public domain: no auth headers
     *
     * Owner and Visitor tokens stay separated except for explicit dual-auth
     * save-to-drive requests.
     */
    class RequestFactory {
    public:
        auto PrepareHeaders(AuthDomain domain) const -> QMap<QString, QString>;

        void SetOwnerAccessToken(const QString& token);
        void SetVisitorShareToken(const QString& token);
        void ClearOwnerToken();
        void ClearVisitorToken();
        auto GetOwnerAccessToken() const -> QString;
        auto GetVisitorShareToken() const -> QString;

    private:
        QString m_owner_access_token;
        QString m_visitor_share_token;
    };

} // namespace disk::desktop
