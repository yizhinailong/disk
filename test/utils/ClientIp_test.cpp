/**
 * @file ClientIp_test.cpp
 * @brief Trusted reverse-proxy client IP boundary contracts
 */

#include "utils/ClientIp.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace {

    auto RepositoryRoot() -> std::filesystem::path {
        return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    }

    auto ReadSourceFile(const std::filesystem::path& relative_path) -> std::string {
        std::ifstream input(RepositoryRoot() / relative_path);
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }

    auto Contains(const std::string& source, std::string_view expected) -> bool {
        return source.find(expected) != std::string::npos;
    }

    auto CountOccurrences(const std::string& source, std::string_view expected) -> size_t {
        size_t count = 0;
        size_t position = 0;
        while ((position = source.find(expected, position)) != std::string::npos) {
            ++count;
            position += expected.size();
        }
        return count;
    }

    TEST(ClientIpContract, TrustedProxyBoundaryIsCentralized) {
        const auto resolver = ReadSourceFile("src/utils/ClientIp.hpp");
        const auto runtime_config = ReadSourceFile("src/utils/RuntimeConfig.cpp");
        const auto default_config = ReadSourceFile("config.json");
        const auto distributed_config = ReadSourceFile("deploy/config.distributed.json");
        const auto compose = ReadSourceFile("docker-compose.distributed.yml");

        EXPECT_TRUE(Contains(resolver, "ResolveClientIp"));
        EXPECT_TRUE(Contains(resolver, "disk-client-ip"));
        EXPECT_TRUE(Contains(resolver, "attributes->find"));
        EXPECT_TRUE(Contains(resolver, "peerAddr().toIp()"));
        EXPECT_FALSE(Contains(resolver, "getHeader("));

        for (const auto& config : { default_config, distributed_config }) {
            EXPECT_EQ(
                CountOccurrences(
                    config,
                    "\"name\": \"drogon::plugin::RealIpResolver\""
                ),
                1U
            );
            EXPECT_TRUE(Contains(config, "\"from_header\": \"x-real-ip\""));
            EXPECT_TRUE(Contains(config, "\"attribute_key\": \"disk-client-ip\""));
            EXPECT_TRUE(Contains(config, "\"trust_ips\": []"));
        }
        EXPECT_TRUE(Contains(runtime_config, "DISK_TRUSTED_PROXY_CIDRS"));
        EXPECT_TRUE(Contains(runtime_config, "[\"trust_ips\"]"));
        EXPECT_TRUE(Contains(compose, "DISK_TRUSTED_PROXY_CIDRS: '[\"172.28.0.10\"]'"));
        EXPECT_TRUE(Contains(compose, "ipv4_address: 172.28.0.10"));

        const auto auth_controller = ReadSourceFile("src/controllers/AuthController.cpp");
        const auto register_filter = ReadSourceFile("src/filters/RegisterRateLimitFilter.cpp");
        const auto share_filter = ReadSourceFile("src/filters/ShareRateLimitFilter.cpp");
        const auto share_controller = ReadSourceFile("src/controllers/ShareController.cpp");
        const auto recovery_controller =
            ReadSourceFile("src/controllers/StorageRecoveryAdminController.cpp");
        const auto job_controller =
            ReadSourceFile("src/controllers/StorageJobAdminController.cpp");

        EXPECT_EQ(CountOccurrences(auth_controller, "ResolveClientIp(request)"), 2U);
        EXPECT_EQ(CountOccurrences(register_filter, "ResolveClientIp(request)"), 1U);
        EXPECT_EQ(CountOccurrences(share_filter, "ResolveClientIp(request)"), 1U);
        EXPECT_EQ(CountOccurrences(share_controller, "ResolveClientIp(request)"), 1U);
        EXPECT_EQ(CountOccurrences(recovery_controller, "ResolveClientIp(request)"), 1U);
        EXPECT_EQ(CountOccurrences(job_controller, "ResolveClientIp(request)"), 1U);

        for (const auto& source : {
                 auth_controller,
                 register_filter,
                 share_filter,
                 share_controller,
                 recovery_controller,
                 job_controller,
             }) {
            EXPECT_FALSE(Contains(source, "X-Real-IP"));
            EXPECT_FALSE(Contains(source, "X-Forwarded-For"));
        }
    }

    TEST(ClientIp, ResolverAttributeOverridesTransportPeer) {
        const auto request = drogon::HttpRequest::newHttpRequest();
        request->attributes()->insert(
            disk::utils::CLIENT_IP_ATTRIBUTE,
            trantor::InetAddress("198.51.100.27", 0)
        );

        EXPECT_EQ(disk::utils::ResolveClientIp(request), "198.51.100.27");
    }

    TEST(ClientIp, ResolverAttributePreservesIpv6TextWithoutPort) {
        const auto request = drogon::HttpRequest::newHttpRequest();
        request->attributes()->insert(
            disk::utils::CLIENT_IP_ATTRIBUTE,
            trantor::InetAddress("2001:db8::27", 0, true)
        );

        EXPECT_EQ(disk::utils::ResolveClientIp(request), "2001:db8::27");
    }

    TEST(ClientIp, ForwardedHeadersDoNotOverrideMissingAttribute) {
        const auto request = drogon::HttpRequest::newHttpRequest();
        request->addHeader("X-Real-IP", "198.51.100.99");
        request->addHeader("X-Forwarded-For", "203.0.113.99");

        EXPECT_EQ(disk::utils::ResolveClientIp(request), request->peerAddr().toIp());
    }

} // namespace
