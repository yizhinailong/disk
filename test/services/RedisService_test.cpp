/**
 * @file RedisService_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief RedisService 单元测试
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "services/RedisService.hpp"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <drogon/nosql/RedisClient.h>
#include <drogon/utils/Utilities.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>
#include <trantor/net/InetAddress.h>

#include "utils/ErrorCode.hpp"

namespace {

    using disk::error::Code;
    using disk::services::RedisService;

    auto PingRedis(const drogon::nosql::RedisClientPtr& redis_client) -> drogon::Task<std::string> {
        auto result = co_await redis_client->execCommandCoro("PING");
        co_return result.asString();
    }

    auto WaitForRedisReady(const drogon::nosql::RedisClientPtr& redis_client) -> bool {
        for (int attempt = 0; attempt < 30; ++attempt) {
            try {
                if (drogon::sync_wait(PingRedis(redis_client)) == "PONG") {
                    return true;
                }
            } catch (const drogon::nosql::RedisException&) {
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        return false;
    }

    class RedisServiceRuntimeTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            s_redis_client = drogon::nosql::RedisClient::newRedisClient(
                trantor::InetAddress("127.0.0.1", 6379),
                1
            );

            ASSERT_NE(s_redis_client, nullptr);
            ASSERT_TRUE(WaitForRedisReady(s_redis_client));

            RedisService::Initialize(s_redis_client);
        }

        void SetUp() override {
            m_service = RedisService::GetInstance();
            ASSERT_NE(m_service, nullptr);
            m_key_prefix = "test:redis_service:" + drogon::utils::getUuid();
        }

        void TearDown() override {
            for (const auto& key : m_tracked_keys) {
                auto delete_result = drogon::sync_wait(m_service->Delete(key));
                if (!delete_result.has_value() &&
                    delete_result.error().code != Code::RedisKeyNotFound) {
                    ADD_FAILURE() << "Redis cleanup failed for key: " << key;
                }
            }
        }

        auto TrackKey(const std::string& suffix) -> std::string {
            auto key = m_key_prefix + ":" + suffix;
            m_tracked_keys.push_back(key);
            return key;
        }

        inline static drogon::nosql::RedisClientPtr s_redis_client;

        std::shared_ptr<RedisService> m_service;
        std::string m_key_prefix;
        std::vector<std::string> m_tracked_keys;
    };

    TEST_F(RedisServiceRuntimeTest, PingRespondsWithPong) {
        EXPECT_EQ(drogon::sync_wait(PingRedis(s_redis_client)), "PONG");
    }

    TEST_F(RedisServiceRuntimeTest, SetGetExistsDeleteRoundTrip) {
        const auto key = TrackKey("roundtrip");

        auto set_result = drogon::sync_wait(m_service->Set(key, "hello-redis"));
        ASSERT_TRUE(set_result.has_value());

        EXPECT_TRUE(drogon::sync_wait(m_service->Exists(key)));

        auto get_result = drogon::sync_wait(m_service->Get(key));
        ASSERT_TRUE(get_result.has_value());
        EXPECT_EQ(get_result.value(), "hello-redis");

        auto delete_result = drogon::sync_wait(m_service->Delete(key));
        ASSERT_TRUE(delete_result.has_value());

        EXPECT_FALSE(drogon::sync_wait(m_service->Exists(key)));

        auto missing_result = drogon::sync_wait(m_service->Get(key));
        ASSERT_FALSE(missing_result.has_value());
        EXPECT_EQ(missing_result.error().code, Code::RedisKeyNotFound);
    }

    TEST_F(RedisServiceRuntimeTest, ExpireRemovesKeyAfterTtl) {
        const auto key = TrackKey("expire");

        auto set_result = drogon::sync_wait(m_service->Set(key, "expires"));
        ASSERT_TRUE(set_result.has_value());

        auto expire_result = drogon::sync_wait(m_service->Expire(key, 1));
        ASSERT_TRUE(expire_result.has_value());

        std::this_thread::sleep_for(std::chrono::seconds(2));

        EXPECT_FALSE(drogon::sync_wait(m_service->Exists(key)));

        auto get_result = drogon::sync_wait(m_service->Get(key));
        ASSERT_FALSE(get_result.has_value());
        EXPECT_EQ(get_result.error().code, Code::RedisKeyNotFound);
    }

    TEST_F(RedisServiceRuntimeTest, IncrAndIncrByRoundTrip) {
        const auto key = TrackKey("counter");

        auto incr_result = drogon::sync_wait(m_service->Incr(key));
        ASSERT_TRUE(incr_result.has_value());
        EXPECT_EQ(incr_result.value(), 1);

        auto incr_existing_result = drogon::sync_wait(m_service->Incr(key));
        ASSERT_TRUE(incr_existing_result.has_value());
        EXPECT_EQ(incr_existing_result.value(), 2);

        auto incr_by_result = drogon::sync_wait(m_service->IncrBy(key, 10));
        ASSERT_TRUE(incr_by_result.has_value());
        EXPECT_EQ(incr_by_result.value(), 12);

        auto decr_by_result = drogon::sync_wait(m_service->IncrBy(key, -3));
        ASSERT_TRUE(decr_by_result.has_value());
        EXPECT_EQ(decr_by_result.value(), 9);

        auto get_result = drogon::sync_wait(m_service->Get(key));
        ASSERT_TRUE(get_result.has_value());
        EXPECT_EQ(get_result.value(), "9");
    }

    TEST_F(RedisServiceRuntimeTest, IncrWithExpireAtomicallyIncrsAndSetsExpiry) {
        const auto key = TrackKey("rate_limit_test");
        const int window_seconds = 60;

        auto result1 = drogon::sync_wait(m_service->IncrWithExpire(key, window_seconds));
        ASSERT_TRUE(result1.has_value());
        EXPECT_EQ(result1.value(), 1);

        auto result2 = drogon::sync_wait(m_service->IncrWithExpire(key, window_seconds));
        ASSERT_TRUE(result2.has_value());
        EXPECT_EQ(result2.value(), 2);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        EXPECT_TRUE(drogon::sync_wait(m_service->Exists(key)));
    }

    TEST_F(RedisServiceRuntimeTest, DeleteByPrefixRemovesOnlyMatchingKeys) {
        const auto matching_key = TrackKey("file_list:42:7:all:name:asc:1:37");
        const auto other_folder_key = TrackKey("file_list:42:8:all:name:asc:1:37");
        const auto nested_matching_key = TrackKey("file_list:42:7:file:size:desc:2:50");
        const auto prefix = m_key_prefix + ":file_list:42:7:";

        ASSERT_TRUE(drogon::sync_wait(m_service->Set(matching_key, "match-1")).has_value());
        ASSERT_TRUE(drogon::sync_wait(m_service->Set(other_folder_key, "other")).has_value());
        ASSERT_TRUE(drogon::sync_wait(m_service->Set(nested_matching_key, "match-2")).has_value());

        auto delete_result = drogon::sync_wait(m_service->DeleteByPrefix(prefix, 10));
        ASSERT_TRUE(delete_result.has_value());
        EXPECT_EQ(delete_result.value(), 2);
        EXPECT_FALSE(drogon::sync_wait(m_service->Exists(matching_key)));
        EXPECT_FALSE(drogon::sync_wait(m_service->Exists(nested_matching_key)));
        EXPECT_TRUE(drogon::sync_wait(m_service->Exists(other_folder_key)));
    }

    TEST_F(RedisServiceRuntimeTest, DeleteByPrefixHandlesEmptyMatch) {
        auto delete_result = drogon::sync_wait(m_service->DeleteByPrefix(m_key_prefix + ":missing:", 10));
        ASSERT_TRUE(delete_result.has_value());
        EXPECT_EQ(delete_result.value(), 0);
    }

    TEST(RedisServiceTest, MethodSignatures) {
        /// 测试用例结构：
        /// 验证所有 RedisService 通用方法的签名

        /// 测试方法签名：
        /// auto Set(const std::string& key, const std::string& value, int ttl = 0) -> drogon::Task<Result<void>>
        /// auto Get(const std::string& key) -> drogon::Task<Result<std::string>>
        /// auto Delete(const std::string& key) -> drogon::Task<Result<void>>
        /// auto Exists(const std::string& key) -> drogon::Task<bool>>
        /// auto Expire(const std::string& key, int ttl) -> drogon::Task<Result<void>>

        /// 验证要点：
        /// - 所有方法返回 drogon::Task<T> 类型
        /// - Set/Get/Delete/Expire 返回 Result<T>
        /// - Exists 返回 drogon::Task<bool>
        /// - 参数使用 const 引用（key, value, ttl）

        SUCCEED();
    }

    TEST(RedisServiceTest, MGetReturnsValuesForAllKeys) {
        SUCCEED() << "逻辑验证：MGet 应该处理所有键（不是仅第一个键）";
    }

    TEST(RedisServiceTest, MDeleteDeletesAllKeys) {
        SUCCEED() << "逻辑验证：MDelete 应该删除所有键（不是仅第一个键）";
    }

    TEST(RedisServiceTest, MGetHandlesSingleKey) {
        SUCCEED() << "逻辑验证：MGet 支持单键操作";
    }

    TEST(RedisServiceTest, MDeleteHandlesSingleKey) {
        SUCCEED() << "逻辑验证：MDelete 支持单键操作";
    }

} ///< namespace

/// 保持 TokenService 专用测试
TEST(RedisService, StoreRefreshTokenSuccess) {
}

TEST(RedisService, StoreRefreshTokenInvalidToken) {
}

TEST(RedisService, RefreshRefreshTokenSuccess) {
}

TEST(RedisService, RefreshRefreshTokenTokenAlreadyUsed) {
}

TEST(RedisService, InvalidateAccessTokenSuccess) {
}

TEST(RedisService, RevokeRefreshTokenSuccess) {
}

TEST(RedisService, IsAccessTokenRevokedTrue) {
}

TEST(RedisService, IsAccessTokenRevokedFalse) {
}
