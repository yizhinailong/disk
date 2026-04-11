/**
 * @file ShareAuthFilter_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief ShareAuthFilter 单元测试
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "filters/ShareAuthFilter.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <drogon/nosql/RedisClient.h>
#include <drogon/utils/Utilities.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>
#include <trantor/net/InetAddress.h>

#include "services/RedisService.hpp"
#include "services/TokenService.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/RedisKeyPrefix.hpp"

namespace {

    using disk::error::Code;
    using disk::filters::ShareAuthFilter;
    using disk::services::RedisService;
    using disk::services::TokenService;

    constexpr const char* TEST_JWT_SECRET = "test_secret_key_for_share_token_32b";

    auto WaitForRedisReady(const drogon::nosql::RedisClientPtr& redis_client) -> bool {
        for (int attempt = 0; attempt < 30; ++attempt) {
            try {
                auto ping_result = drogon::sync_wait([&]() -> drogon::Task<std::string> {
                    auto result = co_await redis_client->execCommandCoro("PING");
                    co_return result.asString();
                }());
                if (ping_result == "PONG") {
                    return true;
                }
            } catch (const drogon::nosql::RedisException&) {
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        return false;
    }

    class ShareAuthFilterRuntimeTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            TokenService::Initialize(TEST_JWT_SECRET);

            s_redis_client = drogon::nosql::RedisClient::newRedisClient(
                trantor::InetAddress("127.0.0.1", 6379),
                1
            );

            ASSERT_NE(s_redis_client, nullptr);
            ASSERT_TRUE(WaitForRedisReady(s_redis_client));

            RedisService::Initialize(s_redis_client);
        }

        void SetUp() override {
            m_filter = std::make_unique<ShareAuthFilter>();
            m_token_service = TokenService::GetInstance();
            m_redis_service = RedisService::GetInstance();

            ASSERT_NE(m_filter, nullptr);
            ASSERT_NE(m_token_service, nullptr);
            ASSERT_NE(m_redis_service, nullptr);
        }

        void TearDown() override {
            for (const auto& key : m_cleanup_keys) {
                auto delete_result = drogon::sync_wait(m_redis_service->Delete(key));
                if (!delete_result.has_value() &&
                    delete_result.error().code != Code::RedisKeyNotFound) {
                    ADD_FAILURE() << "Redis cleanup failed for key: " << key;
                }
            }
            m_token_service->ClearShareRevocationCache();
        }

        auto BuildShareRequestWithToken(const std::string& token) const -> drogon::HttpRequestPtr {
            auto request = drogon::HttpRequest::newHttpRequest();
            request->setMethod(drogon::Get);
            request->setPath("/api/share/browse/runtime-share");
            request->addHeader("X-Share-Token", token);
            return request;
        }

        auto TrackRevocationKey(const std::string& token) -> void {
            auto hash_result = TokenService::ExtractShareTokenHash(token);
            ASSERT_TRUE(hash_result.has_value());

            m_cleanup_keys.push_back(
                disk::redis::RedisKeyPrefix::BuildShareTokenBlacklistKey(hash_result.value())
            );
        }

        inline static drogon::nosql::RedisClientPtr s_redis_client;

        std::unique_ptr<ShareAuthFilter> m_filter;
        std::shared_ptr<TokenService> m_token_service;
        std::shared_ptr<RedisService> m_redis_service;
        std::vector<std::string> m_cleanup_keys;
    };

    TEST(ShareAuthFilterTest, ErrorMappingContractTokenMissing) {
        auto http_status = disk::error::GetHttpStatus(Code::TokenMissing);
        EXPECT_EQ(http_status, drogon::k401Unauthorized);
        auto message = disk::error::GetErrorMessage(Code::TokenMissing);
        EXPECT_EQ(message, std::string("Token not provided"));
    }

    TEST(ShareAuthFilterTest, ErrorMappingContractTokenMalformed) {
        auto http_status = disk::error::GetHttpStatus(Code::TokenMalformed);
        EXPECT_EQ(http_status, drogon::k401Unauthorized);
        auto message = disk::error::GetErrorMessage(Code::TokenMalformed);
        EXPECT_EQ(message, std::string("Token format error"));
    }

    TEST(ShareAuthFilterTest, ErrorMappingContractTokenExpired) {
        auto http_status = disk::error::GetHttpStatus(Code::TokenExpired);
        EXPECT_EQ(http_status, drogon::k401Unauthorized);
        auto message = disk::error::GetErrorMessage(Code::TokenExpired);
        EXPECT_EQ(message, std::string("Token expired"));
    }

    TEST(ShareAuthFilterTest, ErrorMappingContractTokenRevoked) {
        auto http_status = disk::error::GetHttpStatus(Code::TokenRevoked);
        EXPECT_EQ(http_status, drogon::k401Unauthorized);
        auto message = disk::error::GetErrorMessage(Code::TokenRevoked);
        EXPECT_EQ(message, std::string("Token revoked"));
    }

    TEST(ShareAuthFilterTest, ErrorCodesAreDistinct) {
        EXPECT_NE(
            static_cast<uint32_t>(Code::TokenMissing),
            static_cast<uint32_t>(Code::TokenMalformed)
        );
        EXPECT_NE(
            static_cast<uint32_t>(Code::TokenMissing),
            static_cast<uint32_t>(Code::TokenExpired)
        );
        EXPECT_NE(
            static_cast<uint32_t>(Code::TokenMissing),
            static_cast<uint32_t>(Code::TokenRevoked)
        );
        EXPECT_NE(
            static_cast<uint32_t>(Code::TokenMalformed),
            static_cast<uint32_t>(Code::TokenExpired)
        );
        EXPECT_NE(
            static_cast<uint32_t>(Code::TokenMalformed),
            static_cast<uint32_t>(Code::TokenRevoked)
        );
        EXPECT_NE(
            static_cast<uint32_t>(Code::TokenExpired),
            static_cast<uint32_t>(Code::TokenRevoked)
        );
    }

    TEST(ShareAuthFilterTest, ServiceVerifyTokenMalformedPathReturnsMalformedCode) {
        auto result = TokenService::VerifyShareToken(TEST_JWT_SECRET, "malformed.token.string");
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, Code::TokenMalformed);
    }

    TEST(ShareAuthFilterTest, ServiceVerifyTokenEmptyPathReturnsMalformedCode) {
        auto result = TokenService::VerifyShareToken(TEST_JWT_SECRET, "");
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, Code::TokenMalformed);
    }

    TEST_F(ShareAuthFilterRuntimeTest, FilterMissingTokenReturnsTokenMissing) {
        auto request = drogon::HttpRequest::newHttpRequest();
        request->setMethod(drogon::Get);
        request->setPath("/api/share/browse/runtime-share");

        auto response = drogon::sync_wait(m_filter->doFilter(request));
        ASSERT_NE(response, nullptr);
        EXPECT_EQ(response->getStatusCode(), drogon::k401Unauthorized);

        auto json = response->getJsonObject();
        ASSERT_NE(json, nullptr);
        EXPECT_EQ((*json)["code"].asUInt(), static_cast<Json::UInt>(Code::TokenMissing));
        EXPECT_EQ((*json)["message"].asString(), "Token not provided");
    }

    TEST_F(ShareAuthFilterRuntimeTest, FilterRevokedTokenReturnsTokenRevoked) {
        auto token_result = TokenService::GenerateShareToken(
            TEST_JWT_SECRET,
            "runtime-share-revoked",
            9001
        );
        ASSERT_TRUE(token_result.has_value());

        auto revoke_result = drogon::sync_wait(m_token_service->RevokeShareToken(token_result.value()));
        ASSERT_TRUE(revoke_result.has_value());
        TrackRevocationKey(token_result.value());

        auto request = BuildShareRequestWithToken(token_result.value());
        auto response = drogon::sync_wait(m_filter->doFilter(request));
        ASSERT_NE(response, nullptr);
        EXPECT_EQ(response->getStatusCode(), drogon::k401Unauthorized);

        auto json = response->getJsonObject();
        ASSERT_NE(json, nullptr);
        EXPECT_EQ((*json)["code"].asUInt(), static_cast<Json::UInt>(Code::TokenRevoked));
        EXPECT_EQ((*json)["message"].asString(), "Token revoked");
    }

    TEST_F(ShareAuthFilterRuntimeTest, FilterValidTokenSetsRequestAttributes) {
        auto token_result = TokenService::GenerateShareToken(
            TEST_JWT_SECRET,
            "runtime-share-valid",
            42
        );
        ASSERT_TRUE(token_result.has_value());

        auto request = BuildShareRequestWithToken(token_result.value());
        auto response = drogon::sync_wait(m_filter->doFilter(request));
        EXPECT_EQ(response, nullptr);

        ASSERT_TRUE(request->attributes()->find("share_code"));
        ASSERT_TRUE(request->attributes()->find("share_id"));
        EXPECT_EQ(request->attributes()->get<std::string>("share_code"), "runtime-share-valid");
        EXPECT_EQ(request->attributes()->get<uint64_t>("share_id"), 42u);
    }

    TEST_F(ShareAuthFilterRuntimeTest, ValidTokenPopulatesShareRevocationCache) {
        m_token_service->ClearShareRevocationCache();
        EXPECT_EQ(m_token_service->GetShareRevocationCacheSizeForTest(), 0u);

        auto token_result = TokenService::GenerateShareToken(
            TEST_JWT_SECRET,
            "cache-populate-test",
            55
        );
        ASSERT_TRUE(token_result.has_value());

        auto request = BuildShareRequestWithToken(token_result.value());
        auto response = drogon::sync_wait(m_filter->doFilter(request));
        EXPECT_EQ(response, nullptr);

        EXPECT_EQ(m_token_service->GetShareRevocationCacheSizeForTest(), 1u);
    }

    TEST_F(ShareAuthFilterRuntimeTest, RepeatedValidTokenUsesCacheNoGrowth) {
        m_token_service->ClearShareRevocationCache();

        auto token_result = TokenService::GenerateShareToken(
            TEST_JWT_SECRET,
            "cache-hit-test",
            77
        );
        ASSERT_TRUE(token_result.has_value());

        auto request1 = BuildShareRequestWithToken(token_result.value());
        auto response1 = drogon::sync_wait(m_filter->doFilter(request1));
        EXPECT_EQ(response1, nullptr);
        EXPECT_EQ(m_token_service->GetShareRevocationCacheSizeForTest(), 1u);

        auto request2 = BuildShareRequestWithToken(token_result.value());
        auto response2 = drogon::sync_wait(m_filter->doFilter(request2));
        EXPECT_EQ(response2, nullptr);
        EXPECT_EQ(m_token_service->GetShareRevocationCacheSizeForTest(), 1u);
    }

} // namespace
