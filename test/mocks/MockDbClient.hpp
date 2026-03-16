/**
 * @file MockDbClient.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Mock database client for testing
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include <drogon/orm/DbClient.h>
#include <drogon/orm/Mapper.h>
#include <drogon/orm/Result.h>
#include <gmock/gmock.h>

namespace disk::test {

    /**
     * @brief Mock database client
     *
     * Provides simplified interface for database operations used in tests.
     * Focuses on methods actually used by AuthService.
     */
    class MockDbClient {
    public:
        MockDbClient() = default;
        virtual ~MockDbClient() = default;
        MockDbClient(const MockDbClient&) = delete;
        MockDbClient& operator=(const MockDbClient&) = delete;
        MockDbClient(MockDbClient&&) = delete;
        MockDbClient& operator=(MockDbClient&&) = delete;

        // Mock 计数操作的同步 SQL 执行
        MOCK_METHOD(
            std::size_t,
            execSqlSync,
            (const std::string&),
            (noexcept)
        );

        // Mock 插入/更新操作的同步 SQL 执行
        MOCK_METHOD(
            std::size_t,
            execSqlSync,
            (const std::string&, const std::size_t),
            (noexcept)
        );
    };

    inline auto CreateMockDbClient() -> std::shared_ptr<MockDbClient> {
        return std::make_shared<MockDbClient>();
    }

} // namespace disk::test
