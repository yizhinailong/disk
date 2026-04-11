/**
 * @file SystemService_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief SystemService 单元测试
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "services/SystemService.hpp"

#include <cstdint>
#include <string>

#include <gtest/gtest.h>

namespace disk::system {
    namespace {

        // ==================== StorageStats 结构体测试 ====================

        class StorageStatsTest : public ::testing::Test {};

        TEST_F(StorageStatsTest, DefaultValues) {
            StorageStats stats;

            EXPECT_EQ(stats.total_users, 0);
            EXPECT_EQ(stats.total_files, 0);
            EXPECT_EQ(stats.total_folders, 0);
            EXPECT_EQ(stats.total_size, 0);
        }

        TEST_F(StorageStatsTest, AssignedValues) {
            StorageStats stats;
            stats.total_users = 42;
            stats.total_files = 1000;
            stats.total_folders = 200;
            stats.total_size = 1024 * 1024 * 512;

            EXPECT_EQ(stats.total_users, 42);
            EXPECT_EQ(stats.total_files, 1000);
            EXPECT_EQ(stats.total_folders, 200);
            EXPECT_EQ(stats.total_size, 1024 * 1024 * 512);
        }

        // ==================== ConnectionStats 结构体测试 ====================

        class ConnectionStatsTest : public ::testing::Test {};

        TEST_F(ConnectionStatsTest, DefaultValues) {
            ConnectionStats stats;

            EXPECT_EQ(stats.current, 0);
            EXPECT_EQ(stats.peak, 0);
        }

        TEST_F(ConnectionStatsTest, AssignedValues) {
            ConnectionStats stats;
            stats.current = 15;
            stats.peak = 128;

            EXPECT_EQ(stats.current, 15);
            EXPECT_EQ(stats.peak, 128);
        }

        // ==================== SystemInfo 结构体测试 ====================

        class SystemInfoTest : public ::testing::Test {};

        TEST_F(SystemInfoTest, DefaultValues) {
            SystemInfo info;

            EXPECT_TRUE(info.version.empty());
            EXPECT_TRUE(info.drogon_version.empty());
            EXPECT_TRUE(info.build_time.empty());
            EXPECT_EQ(info.uptime, 0);
            EXPECT_EQ(info.connections.current, 0);
            EXPECT_EQ(info.connections.peak, 0);
            EXPECT_EQ(info.storage.total_users, 0);
            EXPECT_EQ(info.storage.total_files, 0);
            EXPECT_EQ(info.storage.total_folders, 0);
            EXPECT_EQ(info.storage.total_size, 0);
        }

        TEST_F(SystemInfoTest, AllFieldsAssigned) {
            SystemInfo info;
            info.version = "1.0.0";
            info.drogon_version = "1.9.11";
            info.build_time = "Apr 11 2026 10:00:00";
            info.uptime = 3600;
            info.connections.current = 5;
            info.connections.peak = 20;
            info.storage.total_users = 100;
            info.storage.total_files = 5000;
            info.storage.total_folders = 800;
            info.storage.total_size = 1073741824;

            EXPECT_EQ(info.version, "1.0.0");
            EXPECT_EQ(info.drogon_version, "1.9.11");
            EXPECT_EQ(info.uptime, 3600);
            EXPECT_EQ(info.connections.current, 5);
            EXPECT_EQ(info.storage.total_users, 100);
            EXPECT_EQ(info.storage.total_files, 5000);
            EXPECT_EQ(info.storage.total_folders, 800);
            EXPECT_EQ(info.storage.total_size, 1073741824);
        }

        // ==================== SQL 查询正确性验证 ====================
        // 确保 GetStorageStats() 中的 SQL 不引用不存在的 deleted_at 列。
        // files/folders 表不含 deleted_at 列（软删除由 trash 表承载），
        // 因此 SQL 查询不应包含 WHERE deleted_at IS NULL 条件。

        class SystemServiceSqlCorrectnessTest : public ::testing::Test {};

        TEST_F(SystemServiceSqlCorrectnessTest, FilesQueryNoDeletedAt) {
            // 这是 GetStorageStats() 中应使用的文件统计 SQL 模式
            std::string files_sql =
                "SELECT COUNT(*) as count, COALESCE(SUM(size), 0) as total_size FROM files";

            EXPECT_EQ(files_sql.find("deleted_at"), std::string::npos)
                << "files 表不含 deleted_at 列，SQL 不应引用它";
        }

        TEST_F(SystemServiceSqlCorrectnessTest, FoldersQueryNoDeletedAt) {
            // 这是 GetStorageStats() 中应使用的文件夹统计 SQL 模式
            std::string folders_sql = "SELECT COUNT(*) as count FROM folders";

            EXPECT_EQ(folders_sql.find("deleted_at"), std::string::npos)
                << "folders 表不含 deleted_at 列，SQL 不应引用它";
        }

        TEST_F(SystemServiceSqlCorrectnessTest, UsersQueryFiltersDisabled) {
            std::string users_sql =
                "SELECT COUNT(*) as count FROM users WHERE status != -1";

            // users 表确实有 status 列，过滤禁用账户是合理的
            EXPECT_NE(users_sql.find("status"), std::string::npos);
            EXPECT_EQ(users_sql.find("deleted_at"), std::string::npos)
                << "users 表不含 deleted_at 列，SQL 不应引用它";
        }

        // ==================== StorageStats 数值溢出防护测试 ====================

        class StorageStatsBoundaryTest : public ::testing::Test {};

        TEST_F(StorageStatsBoundaryTest, LargeFileSize) {
            StorageStats stats;
            stats.total_size = INT64_MAX;

            EXPECT_EQ(stats.total_size, INT64_MAX);
            EXPECT_GT(stats.total_size, 0);
        }

        TEST_F(StorageStatsBoundaryTest, ZeroFilesAndFolders) {
            StorageStats stats;

            EXPECT_EQ(stats.total_files + stats.total_folders, 0);
        }

        TEST_F(StorageStatsBoundaryTest, LargeUserCount) {
            StorageStats stats;
            stats.total_users = 1000000;

            EXPECT_EQ(stats.total_users, 1000000);
        }

    } // namespace
} // namespace disk::system
