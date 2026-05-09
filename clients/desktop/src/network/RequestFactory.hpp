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
                            Visitor };

    /**
     * @brief Creates pre-configured request headers based on auth domain
     *
     * Owner domain: injects Authorization: Bearer <access_token>
     * Visitor domain: injects X-Share-Token: <share_token>
     * Public domain: no auth headers
     *
     * Tokens are NEVER mixed: Owner requests never carry X-Share-Token,
     * Visitor requests never carry Authorization.
     */
    class RequestFactory {
    public:
        auto PrepareHeaders(AuthDomain domain) const -> QMap<QString, QString>;

        void SetOwnerAccessToken(const QString& token);
        void SetVisitorShareToken(const QString& token);
        void ClearOwnerToken();
        void ClearVisitorToken();

    private:
        QString m_owner_access_token;
        QString m_visitor_share_token;
    };

} // namespace disk::desktop
