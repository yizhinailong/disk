/**
 * @file RequestFactory.cpp
 * @brief Auth-domain-aware request factory implementation
 *
 * @copyright Copyright (c) 2026
 */

#include "network/RequestFactory.hpp"

namespace disk::desktop {

    auto RequestFactory::PrepareHeaders(AuthDomain domain) const
        -> QMap<QString, QString> {
        QMap<QString, QString> headers;

        switch (domain) {
            case AuthDomain::Owner: {
                if (!m_owner_access_token.isEmpty()) {
                    headers["Authorization"] =
                        "Bearer " + m_owner_access_token;
                }
                break;
            }
            case AuthDomain::Visitor: {
                if (!m_visitor_share_token.isEmpty()) {
                    headers["X-Share-Token"] = m_visitor_share_token;
                }
                break;
            }
            case AuthDomain::Public:
                break;
        }

        return headers;
    }

    void RequestFactory::SetOwnerAccessToken(const QString& token) {
        m_owner_access_token = token;
    }

    void RequestFactory::SetVisitorShareToken(const QString& token) {
        m_visitor_share_token = token;
    }

    void RequestFactory::ClearOwnerToken() {
        m_owner_access_token.clear();
    }

    void RequestFactory::ClearVisitorToken() {
        m_visitor_share_token.clear();
    }

    } // namespace disk::desktop
