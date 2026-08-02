/**
 * @file ClientIp.hpp
 * @brief Resolve the client IP from the trusted reverse-proxy boundary
 */

#pragma once

#include <string>

#include <drogon/HttpRequest.h>
#include <trantor/net/InetAddress.h>

namespace disk::utils {

    inline constexpr auto CLIENT_IP_ATTRIBUTE = "disk-client-ip";

    [[nodiscard]]
    inline auto ResolveClientIp(const drogon::HttpRequestPtr& request) -> std::string {
        const auto attributes = request->attributes();
        if (attributes != nullptr && attributes->find(CLIENT_IP_ATTRIBUTE)) {
            return attributes->get<trantor::InetAddress>(CLIENT_IP_ATTRIBUTE).toIp();
        }
        return request->peerAddr().toIp();
    }

} // namespace disk::utils
