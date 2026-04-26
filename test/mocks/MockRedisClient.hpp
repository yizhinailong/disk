/**
 * @file MockRedisClient.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Mock Redis client for testing
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <string>
#include <vector>

#include <drogon/nosql/RedisClient.h>
#include <gmock/gmock.h>

namespace disk::test {

    /**
     * @brief Mock Redis client
     *
     * Provides simplified interface for Redis operations used in tests.
     * Focuses on the commands actually used by TokenService.
     */
    class MockRedisClient {
    public:
        MockRedisClient() = default;
        virtual ~MockRedisClient() = default;
        MockRedisClient(const MockRedisClient&) = delete;
        MockRedisClient& operator=(const MockRedisClient&) = delete;
        MockRedisClient(MockRedisClient&&) = delete;
        MockRedisClient& operator=(MockRedisClient&&) = delete;

        // Mock 不同返回类型的 execCommand
        MOCK_METHOD(
            drogon::nosql::RedisResult,
            execCommandSync,
            (const std::vector<std::string>&),
            (noexcept)
        );
    };

    /**
     * @brief Convert MockRedisClient to RedisClientPtr for use in services
     *
     * This is a helper function that creates a shared_ptr wrapper around MockRedisClient.
     * Note: This is a simplified approach - in production, you'd use actual Redis client.
     */
    inline auto CreateMockRedisClient() -> std::shared_ptr<MockRedisClient> {
        return std::make_shared<MockRedisClient>();
    }

} // namespace disk::test
