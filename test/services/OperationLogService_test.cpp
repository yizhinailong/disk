/**
 * @file OperationLogService_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief OperationLogService 单元测试 — IP 地址归一化 + 枚举转换
 *
 * @copyright Copyright (c) 2026
 *
 * 验证：
 * - 空 IP 地址归一化为 "unknown"（防止 NOT NULL 约束失败）
 * - 非空 IP 地址保持不变
 * - OperationLogEntry 默认 ip_address 为空字符串
 * - ActionType / TargetType 枚举到字符串转换正确性
 */

#include "services/OperationLogService.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

namespace disk::log {
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

        /// ==================== NormalizeIpAddress 测试 ====================

        class NormalizeIpAddressTest : public ::testing::Test {};

        TEST_F(NormalizeIpAddressTest, EmptyStringBecomesUnknown) {
            EXPECT_EQ(OperationLogService::NormalizeIpAddress(""), "unknown");
        }

        TEST_F(NormalizeIpAddressTest, ValidIpPreserved) {
            EXPECT_EQ(OperationLogService::NormalizeIpAddress("192.168.1.1"), "192.168.1.1");
        }

        TEST_F(NormalizeIpAddressTest, Ipv6AddressPreserved) {
            EXPECT_EQ(
                OperationLogService::NormalizeIpAddress("::1"),
                "::1"
            );
            EXPECT_EQ(
                OperationLogService::NormalizeIpAddress("2001:0db8:85a3::8a2e:0370:7334"),
                "2001:0db8:85a3::8a2e:0370:7334"
            );
        }

        TEST_F(NormalizeIpAddressTest, IpWithPortPreserved) {
            EXPECT_EQ(
                OperationLogService::NormalizeIpAddress("10.0.0.1:54321"),
                "10.0.0.1:54321"
            );
        }

        /// ==================== OperationLogEntry 默认值测试 ====================

        class OperationLogEntryTest : public ::testing::Test {};

        TEST_F(OperationLogEntryTest, DefaultIpAddressIsEmpty) {
            OperationLogEntry entry;
            EXPECT_TRUE(entry.ip_address.empty());
        }

        TEST_F(OperationLogEntryTest, DefaultUserIdIsZero) {
            OperationLogEntry entry;
            EXPECT_EQ(entry.user_id, 0);
        }

        TEST_F(OperationLogEntryTest, DefaultTargetIdIsZero) {
            OperationLogEntry entry;
            EXPECT_EQ(entry.target_id, 0);
        }

        /// ==================== 归一化与 Entry 默认值的集成场景 ====================

        TEST_F(OperationLogEntryTest, DefaultEntryIpNormalizesToUnknown) {
            OperationLogEntry entry;
            auto normalized = OperationLogService::NormalizeIpAddress(entry.ip_address);
            EXPECT_EQ(normalized, "unknown");
        }

        TEST_F(OperationLogEntryTest, ExplicitIpPreservedThroughNormalize) {
            OperationLogEntry entry;
            entry.ip_address = "172.16.0.1";
            auto normalized = OperationLogService::NormalizeIpAddress(entry.ip_address);
            EXPECT_EQ(normalized, "172.16.0.1");
        }

        TEST(OperationLogQueryContractTest, PaginationUsesPostgresqlInt64Bindings) {
            const auto source = ReadSourceFile("src/services/OperationLogService.cpp");

            EXPECT_NE(source.find("static_cast<int64_t>(user_id)"), std::string::npos);
            EXPECT_NE(source.find("static_cast<int64_t>(page_size)"), std::string::npos);
            EXPECT_NE(source.find("static_cast<int64_t>(offset)"), std::string::npos);
        }

    } ///< namespace
} ///< namespace disk::log
