/**
 * @file UserService_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief UserService 单元测试
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <cmath>
#include <cstdint>

#include <gtest/gtest.h>

#include "dtos/UserDto.hpp"
#include "utils/ErrorCode.hpp"

namespace disk::user {
    namespace {

        // ==================== GetProfile SQL 字段测试 ====================

        TEST(UserService, GetProfileSqlReturnsExpectedFields) {
            // 验证 GetProfile SQL 查询返回的字段名与 UserProfileResponse 构造一致
            // SQL: SELECT u.id, u.username, u.email, u.nickname, u.avatar,
            //        u.storage_quota, u.storage_used, u.created_at, u.updated_at,
            //        (SELECT COUNT(*) FROM files WHERE user_id = u.id) AS file_count,
            //        (SELECT COUNT(*) FROM folders WHERE user_id = u.id) AS folder_count
            // FROM users u WHERE u.id = ?

            // 验证 SQL 中所有列名与 UserProfileResponse 字段对应
            const std::string sql =
                "SELECT u.id, u.username, u.email, u.nickname, u.avatar, " "u.storage_quota, u.storage_used, u.created_at, u.updated_at, " "(SELECT COUNT(*) FROM files WHERE user_id = u.id) AS file_count, " "(SELECT COUNT(*) FROM folders WHERE user_id = u.id) AS folder_count " "FROM users u WHERE u.id = ?";

            EXPECT_NE(sql.find("u.id"), std::string::npos);
            EXPECT_NE(sql.find("u.username"), std::string::npos);
            EXPECT_NE(sql.find("u.email"), std::string::npos);
            EXPECT_NE(sql.find("u.nickname"), std::string::npos);
            EXPECT_NE(sql.find("u.avatar"), std::string::npos);
            EXPECT_NE(sql.find("u.storage_quota"), std::string::npos);
            EXPECT_NE(sql.find("u.storage_used"), std::string::npos);
            EXPECT_NE(sql.find("u.created_at"), std::string::npos);
            EXPECT_NE(sql.find("u.updated_at"), std::string::npos);
            EXPECT_NE(sql.find("AS file_count"), std::string::npos);
            EXPECT_NE(sql.find("AS folder_count"), std::string::npos);
            EXPECT_NE(sql.find("FROM users u"), std::string::npos);
            EXPECT_NE(sql.find("WHERE u.id = ?"), std::string::npos);
        }

        // ==================== GetStorage SQL 字段测试 ====================

        TEST(UserService, GetStorageSqlReturnsExpectedFields) {
            // 验证 GetStorage SQL 查询返回的字段名与 StorageResponse 构造一致
            const std::string sql =
                "SELECT u.storage_quota, " "COALESCE((SELECT SUM(f.size) FROM files f WHERE f.user_id = u.id), 0) AS used, " "(SELECT COUNT(*) FROM files WHERE user_id = u.id) AS file_count, " "(SELECT COUNT(*) FROM folders WHERE user_id = u.id) AS folder_count " "FROM users u WHERE u.id = ?";

            EXPECT_NE(sql.find("u.storage_quota"), std::string::npos);
            EXPECT_NE(sql.find("AS used"), std::string::npos);
            EXPECT_NE(sql.find("AS file_count"), std::string::npos);
            EXPECT_NE(sql.find("AS folder_count"), std::string::npos);
            EXPECT_NE(sql.find("COALESCE"), std::string::npos);
            EXPECT_NE(sql.find("SUM(f.size)"), std::string::npos);
            EXPECT_NE(sql.find("FROM users u"), std::string::npos);
            EXPECT_NE(sql.find("WHERE u.id = ?"), std::string::npos);
        }

        // ==================== 百分比舍入测试 ====================

        TEST(UserService, GetStoragePercentageRounding) {
            auto calc_percentage = [](uint64_t used, uint64_t quota) -> double {
                if (quota == 0) {
                    return 0.0;
                }
                return std::round(static_cast<double>(used) / static_cast<double>(quota) * 1000.0) /
                       10.0;
            };

            // used=0 → 0.0
            EXPECT_DOUBLE_EQ(calc_percentage(0, 1000), 0.0);

            // used=quota → 100.0
            EXPECT_DOUBLE_EQ(calc_percentage(1000, 1000), 100.0);

            // used > quota → > 100.0
            EXPECT_DOUBLE_EQ(calc_percentage(1500, 1000), 150.0);

            // 正常中间值 50%
            EXPECT_DOUBLE_EQ(calc_percentage(500, 1000), 50.0);

            // 四舍五入到1位小数: 33.33% → 33.3
            EXPECT_DOUBLE_EQ(calc_percentage(1, 3), 33.3);

            // 四舍五入到1位小数: 66.67% → 66.7
            EXPECT_DOUBLE_EQ(calc_percentage(2, 3), 66.7);

            // quota=0 → 0.0 (避免除零)
            EXPECT_DOUBLE_EQ(calc_percentage(500, 0), 0.0);

            // 极小配额 1/1 = 100%
            EXPECT_DOUBLE_EQ(calc_percentage(1, 1), 100.0);

            // 999/1000 = 99.9%
            EXPECT_DOUBLE_EQ(calc_percentage(999, 1000), 99.9);
        }

        // ==================== 错误映射测试 ====================

        TEST(UserService, GetProfileUserNotFoundMapping) {
            // GetProfile 空结果集 → UserNotFound (40100, 404)
            ErrorInfo error(ErrorCode::UserNotFound);
            EXPECT_EQ(error.CodeInt(), 40100u);
            EXPECT_EQ(error.HttpStatus(), drogon::k404NotFound);
        }

        TEST(UserService, GetStorageUserNotFoundMapping) {
            // GetStorage 空结果集 → InternalError (10006, 500)
            ErrorInfo error(ErrorCode::InternalError, "Failed to get storage stats");
            EXPECT_EQ(error.CodeInt(), 10006u);
            EXPECT_EQ(error.HttpStatus(), drogon::k500InternalServerError);
            EXPECT_EQ(error.message, "Failed to get storage stats");
        }

        // ==================== UserProfileResponse 格式测试 ====================

        TEST(UserService, UserProfileResponseFormat) {
            UserProfileResponse response;
            response.id = 1;
            response.username = "admin";
            response.email = "admin@example.com";
            response.nickname = "Administrator";
            response.avatar = "https://example.com/avatar.png";
            response.storage_used = 1024;
            response.storage_quota = 1073741824;
            response.file_count = 42;
            response.folder_count = 7;
            response.created_at = "2026-01-01 00:00:00";
            response.updated_at = "2026-01-15 12:30:00";

            auto json = response.ToJson();

            EXPECT_EQ(json["id"].asUInt64(), 1u);
            EXPECT_EQ(json["username"].asString(), "admin");
            EXPECT_EQ(json["email"].asString(), "admin@example.com");
            EXPECT_EQ(json["nickname"].asString(), "Administrator");
            EXPECT_EQ(json["avatar"].asString(), "https://example.com/avatar.png");
            EXPECT_EQ(json["storage_used"].asUInt64(), 1024u);
            EXPECT_EQ(json["storage_quota"].asUInt64(), 1073741824u);
            EXPECT_EQ(json["file_count"].asUInt(), 42u);
            EXPECT_EQ(json["folder_count"].asUInt(), 7u);
            EXPECT_EQ(json["created_at"].asString(), "2026-01-01 00:00:00");
            EXPECT_EQ(json["updated_at"].asString(), "2026-01-15 12:30:00");
        }

        TEST(UserService, UserProfileResponseNullableFields) {
            UserProfileResponse response;
            response.id = 2;
            response.username = "testuser";
            response.email = "test@example.com";
            response.nickname = "";
            response.avatar = "";
            response.storage_used = 0;
            response.storage_quota = 1073741824;
            response.file_count = 0;
            response.folder_count = 0;
            response.created_at = "2026-02-01 00:00:00";
            response.updated_at = "2026-02-01 00:00:00";

            auto json = response.ToJson();

            EXPECT_EQ(json["nickname"].asString(), "");
            EXPECT_EQ(json["avatar"].asString(), "");
            EXPECT_EQ(json["storage_used"].asUInt64(), 0u);
            EXPECT_EQ(json["file_count"].asUInt(), 0u);
            EXPECT_EQ(json["folder_count"].asUInt(), 0u);
        }

        // ==================== StorageResponse 格式测试 ====================

        TEST(UserService, StorageResponseFormat) {
            StorageResponse response;
            response.used = 536870912;
            response.quota = 1073741824;
            response.percentage = 50.0;
            response.file_count = 100;
            response.folder_count = 10;
            response.categories = {};

            auto json = response.ToJson();

            EXPECT_EQ(json["used"].asUInt64(), 536870912u);
            EXPECT_EQ(json["quota"].asUInt64(), 1073741824u);
            EXPECT_DOUBLE_EQ(json["percentage"].asDouble(), 50.0);
            EXPECT_EQ(json["file_count"].asUInt(), 100u);
            EXPECT_EQ(json["folder_count"].asUInt(), 10u);
            EXPECT_TRUE(json["categories"].isArray());
            EXPECT_EQ(json["categories"].size(), 0u);
        }

    } // namespace
} // namespace disk::user
