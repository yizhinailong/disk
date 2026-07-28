/**
 * @file OperationLogService_test.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief OperationLogService 查询与审计所有权合同测试
 *
 * @copyright Copyright (c) 2026
 *
 * 验证：
 * - 当前用户分页查询的 PostgreSQL 绑定与显式日志上下文
 * - 无调用通用写入子图不得回归
 * - Auth、Share、Admin、Storage Job 与 Recovery 保留领域审计写入
 */

#include "services/OperationLogService.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

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

        auto CountOccurrences(const std::string& source, std::string_view expected) -> size_t {
            size_t count = 0;
            size_t position = 0;
            while ((position = source.find(expected, position)) != std::string::npos) {
                ++count;
                position += expected.size();
            }
            return count;
        }

        TEST(OperationLogQueryContractTest, PaginationUsesPostgresqlInt64Bindings) {
            const auto source = ReadSourceFile("src/services/OperationLogService.cpp");

            EXPECT_NE(source.find("static_cast<int64_t>(user_id)"), std::string::npos);
            EXPECT_NE(source.find("static_cast<int64_t>(page_size)"), std::string::npos);
            EXPECT_NE(source.find("static_cast<int64_t>(offset)"), std::string::npos);
        }

        TEST(OperationLogQueryContractTest, RequestBoundaryUsesExplicitRedactedContext) {
            const auto metrics_header = ReadSourceFile("src/services/MetricsService.hpp");
            const auto metrics_source = ReadSourceFile("src/services/MetricsService.cpp");
            const auto controller = ReadSourceFile("src/controllers/OperationLogController.cpp");
            const auto service_header = ReadSourceFile("src/services/OperationLogService.hpp");
            const auto service_source = ReadSourceFile("src/services/OperationLogService.cpp");
            const auto auth_service = ReadSourceFile("src/services/AuthService.cpp");
            const auto share_audit = ReadSourceFile("src/services/ShareAuditService.cpp");
            const auto admin_service = ReadSourceFile("src/services/AdminService.cpp");
            const auto storage_job_admin =
                ReadSourceFile("src/services/StorageJobAdminService.cpp");
            const auto storage_recovery_admin =
                ReadSourceFile("src/services/StorageRecoveryAdminService.cpp");

            ASSERT_FALSE(metrics_header.empty());
            ASSERT_FALSE(metrics_source.empty());
            ASSERT_FALSE(controller.empty());
            ASSERT_FALSE(service_header.empty());
            ASSERT_FALSE(service_source.empty());
            ASSERT_FALSE(auth_service.empty());
            ASSERT_FALSE(share_audit.empty());
            ASSERT_FALSE(admin_service.empty());
            ASSERT_FALSE(storage_job_admin.empty());
            ASSERT_FALSE(storage_recovery_admin.empty());

            EXPECT_NE(metrics_header.find("OperationLog"), std::string::npos);
            EXPECT_NE(metrics_source.find("path == \"/api/logs\""), std::string::npos);
            EXPECT_NE(metrics_source.find("\"operation_log\""), std::string::npos);
            EXPECT_EQ(
                CountOccurrences(
                    service_header,
                    "disk::utils::LogContext log_context = {}"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(
                    controller,
                    "GetRequestLogContext(request, \"operation_log\")"
                ),
                1U
            );
            EXPECT_NE(
                controller.find("GetList(user_id, page, page_size, log_context)"),
                std::string::npos
            );
            EXPECT_EQ(CountOccurrences(controller, "Logger::Info(log_context)"), 1U);
            EXPECT_EQ(CountOccurrences(controller, "Logger::Error(log_context)"), 1U);
            EXPECT_EQ(CountOccurrences(service_source, "Logger::Error(log_context)"), 1U);
            EXPECT_EQ(
                CountOccurrences(
                    service_source,
                    "Logger::Debug(disk::utils::ServiceRuntimeLogContext())"
                ),
                1U
            );
            EXPECT_EQ(service_source.find("Logger::Debug()"), std::string::npos);

            for (const auto marker : {
                     "enum class ActionType",
                     "enum class TargetType",
                     "struct OperationLogEntry",
                     "auto Log(",
                     "NormalizeIpAddress",
                     "ActionTypeToString",
                     "TargetTypeToString",
                 }) {
                EXPECT_EQ(service_header.find(marker), std::string::npos);
            }
            for (const auto marker : {
                     "OperationLogService::Log(",
                     "OperationLogs log",
                     "ActionTypeToString",
                     "TargetTypeToString",
                     "CoroMapper<",
                 }) {
                EXPECT_EQ(service_source.find(marker), std::string::npos);
            }

            EXPECT_NE(
                auth_service.find(
                    "CoroMapper<drogon_model::disk::OperationLogs> mapper(m_db_client)"
                ),
                std::string::npos
            );
            for (const auto* source : {
                     &share_audit,
                     &admin_service,
                     &storage_job_admin,
                     &storage_recovery_admin,
                 }) {
                EXPECT_NE(source->find("INSERT INTO operation_logs"), std::string::npos);
            }

            EXPECT_EQ(controller.find("Logger::Info()"), std::string::npos);
            EXPECT_EQ(controller.find("Logger::Error()"), std::string::npos);
            EXPECT_EQ(controller.find("getPeerAddr"), std::string::npos);
            EXPECT_EQ(controller.find("result.error().message"), std::string::npos);
            EXPECT_EQ(service_source.find("Logger::Error()"), std::string::npos);
            EXPECT_EQ(service_source.find("e.base().what()"), std::string::npos);
        }

    } // namespace
} // namespace disk::log
