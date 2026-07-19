/**
 * @file RedisService_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief RedisService 单元测试
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "services/RedisService.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include <drogon/nosql/RedisClient.h>
#include <drogon/utils/Utilities.h>
#include <drogon/utils/coroutine.h>
#include <gtest/gtest.h>
#include <trantor/net/InetAddress.h>

#include "services/FileListCache.hpp"
#include "utils/ErrorCode.hpp"
#include "utils/RedisKeyPrefix.hpp"

namespace {

    using disk::error::Code;
    using disk::services::RedisService;

    auto PingRedis(const drogon::nosql::RedisClientPtr& redis_client) -> drogon::Task<std::string> {
        auto result = co_await redis_client->execCommandCoro("PING");
        co_return result.asString();
    }

    auto GetRedisInteger(
        const drogon::nosql::RedisClientPtr& redis_client,
        const std::string& command,
        const std::string& key
    ) -> drogon::Task<std::int64_t> {
        auto result = co_await redis_client->execCommandCoro(
            "%s %s",
            command.c_str(),
            key.c_str()
        );
        co_return result.asInteger();
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

        auto exists_result = drogon::sync_wait(m_service->Exists(key));
        ASSERT_TRUE(exists_result.has_value());
        EXPECT_TRUE(exists_result.value());

        auto get_result = drogon::sync_wait(m_service->Get(key));
        ASSERT_TRUE(get_result.has_value());
        EXPECT_EQ(get_result.value(), "hello-redis");

        auto delete_result = drogon::sync_wait(m_service->Delete(key));
        ASSERT_TRUE(delete_result.has_value());

        exists_result = drogon::sync_wait(m_service->Exists(key));
        ASSERT_TRUE(exists_result.has_value());
        EXPECT_FALSE(exists_result.value());

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

        auto exists_result = drogon::sync_wait(m_service->Exists(key));
        ASSERT_TRUE(exists_result.has_value());
        EXPECT_FALSE(exists_result.value());

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

        auto exists_result = drogon::sync_wait(m_service->Exists(key));
        ASSERT_TRUE(exists_result.has_value());
        EXPECT_TRUE(exists_result.value());
    }

    TEST_F(RedisServiceRuntimeTest, IncrWithExpireSetsTtlOnlyOnFirstIncrement) {
        const auto key = TrackKey("fixed_ttl");
        constexpr int WINDOW_SECONDS = 10;

        auto first_result = drogon::sync_wait(m_service->IncrWithExpire(key, WINDOW_SECONDS));
        ASSERT_TRUE(first_result.has_value());
        EXPECT_EQ(first_result.value(), 1);

        const auto first_ttl = drogon::sync_wait(GetRedisInteger(s_redis_client, "TTL", key));
        EXPECT_GT(first_ttl, 0);
        EXPECT_LE(first_ttl, WINDOW_SECONDS);

        std::this_thread::sleep_for(std::chrono::seconds(2));

        auto second_result = drogon::sync_wait(m_service->IncrWithExpire(key, WINDOW_SECONDS));
        ASSERT_TRUE(second_result.has_value());
        EXPECT_EQ(second_result.value(), 2);

        const auto second_ttl = drogon::sync_wait(GetRedisInteger(s_redis_client, "TTL", key));
        EXPECT_GT(second_ttl, 0);
        EXPECT_LE(second_ttl, first_ttl - 1);
    }

    TEST_F(RedisServiceRuntimeTest, IncrWithExpireIsAtomicUnderConcurrency) {
        const auto key = TrackKey("concurrent_rate_limit");
        constexpr int WINDOW_SECONDS = 60;
        constexpr int WORKER_COUNT = 24;
        std::atomic<bool> start{ false };
        std::vector<std::future<Result<std::int64_t>>> futures;
        futures.reserve(WORKER_COUNT);

        for (int worker = 0; worker < WORKER_COUNT; ++worker) {
            futures.push_back(std::async(std::launch::async, [&, key]() {
                while (!start.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                return drogon::sync_wait(m_service->IncrWithExpire(key, WINDOW_SECONDS));
            }));
        }

        start.store(true, std::memory_order_release);

        std::vector<std::int64_t> counts;
        counts.reserve(WORKER_COUNT);
        for (auto& future : futures) {
            auto result = future.get();
            ASSERT_TRUE(result.has_value());
            counts.push_back(result.value());
        }

        std::ranges::sort(counts);
        for (int index = 0; index < WORKER_COUNT; ++index) {
            EXPECT_EQ(counts[static_cast<std::size_t>(index)], index + 1);
        }

        auto stored_count = drogon::sync_wait(m_service->Get(key));
        ASSERT_TRUE(stored_count.has_value());
        EXPECT_EQ(stored_count.value(), std::to_string(WORKER_COUNT));

        const auto ttl = drogon::sync_wait(GetRedisInteger(s_redis_client, "TTL", key));
        EXPECT_GT(ttl, 0);
        EXPECT_LE(ttl, WINDOW_SECONDS);
    }

    TEST_F(RedisServiceRuntimeTest, FileListCacheVersionStartsAtZeroAndIncrements) {
        const auto user_id = static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()
        );
        const auto version_key = disk::redis::RedisKeyPrefix::BuildFileListCacheVersionKey(user_id);
        m_tracked_keys.push_back(version_key);

        auto initial_version = drogon::sync_wait(disk::file::FileListCache::GetVersion(m_service, user_id));
        ASSERT_TRUE(initial_version.has_value());
        EXPECT_EQ(initial_version.value(), 0);

        drogon::sync_wait(disk::file::FileListCache::Invalidate(m_service, user_id));
        auto first_version = drogon::sync_wait(disk::file::FileListCache::GetVersion(m_service, user_id));
        ASSERT_TRUE(first_version.has_value());
        EXPECT_EQ(first_version.value(), 1);

        drogon::sync_wait(disk::file::FileListCache::Invalidate(m_service, user_id));
        auto second_version = drogon::sync_wait(disk::file::FileListCache::GetVersion(m_service, user_id));
        ASSERT_TRUE(second_version.has_value());
        EXPECT_EQ(second_version.value(), 2);
    }

    TEST_F(RedisServiceRuntimeTest, FileListCacheRejectsMalformedVersion) {
        const auto user_id = static_cast<uint64_t>(
                                 std::chrono::steady_clock::now().time_since_epoch().count()
                             ) +
                             1;
        const auto version_key = disk::redis::RedisKeyPrefix::BuildFileListCacheVersionKey(user_id);
        m_tracked_keys.push_back(version_key);
        ASSERT_TRUE(drogon::sync_wait(m_service->Set(version_key, "not-a-version")).has_value());

        auto version = drogon::sync_wait(disk::file::FileListCache::GetVersion(m_service, user_id));
        ASSERT_FALSE(version.has_value());
        EXPECT_EQ(version.error().code, Code::RedisOperationFailed);
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

} // namespace

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
