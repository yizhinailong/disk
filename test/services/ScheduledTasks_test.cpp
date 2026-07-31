#include "services/ScheduledTasks.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "services/StorageJobContract.hpp"

namespace disk::services {
    namespace {
        using namespace std::chrono_literals;

        template <typename T>
        concept HasIsAccepting = requires(const T& scheduler) {
            scheduler.IsAccepting();
        };

        template <typename T>
        concept HasPublicSeedOnce = requires(
            T& scheduler,
            std::chrono::system_clock::time_point now
        ) {
            scheduler.SeedOnce(now);
        };

        static_assert(!HasIsAccepting<ScheduledTasks>);
        static_assert(!HasPublicSeedOnce<ScheduledTasks>);

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

        [[nodiscard]] auto MakeUtcTimePoint(
            std::chrono::year_month_day date,
            std::chrono::hours hour,
            std::chrono::minutes minute = 0min
        ) -> std::chrono::system_clock::time_point {
            return std::chrono::sys_days(date) + hour + minute;
        }

        TEST(ScheduledTasksLogContextContractTest, UsesTypedProcessCorrelationAndFixedFailures) {
            const auto header = ReadSourceFile("src/services/ScheduledTasks.hpp");
            const auto source = ReadSourceFile("src/services/ScheduledTasks.cpp");
            const auto runtime_begin = source.find("auto ScheduledTasks::Initialize(");

            ASSERT_FALSE(header.empty());
            ASSERT_NE(runtime_begin, std::string::npos);
            const auto runtime_source = source.substr(runtime_begin);

            EXPECT_EQ(
                CountOccurrences(runtime_source, "Logger::Info(utils::LogContext"),
                3U
            );
            EXPECT_EQ(
                CountOccurrences(runtime_source, "Logger::Error(utils::LogContext"),
                2U
            );
            EXPECT_EQ(
                CountOccurrences(
                    runtime_source,
                    ".operation = \"storage_job_scheduler\""
                ),
                2U
            );
            EXPECT_EQ(
                CountOccurrences(runtime_source, ".operation = \"storage_job_seed\""),
                3U
            );
            EXPECT_TRUE(Contains(
                runtime_source,
                "<< \"Periodic storage job seed failed\";"
            ));
            EXPECT_TRUE(Contains(
                runtime_source,
                "<< \"Periodic storage job seed cycle failed\";"
            ));
            EXPECT_FALSE(Contains(runtime_source, "Logger::Info()"));
            EXPECT_FALSE(Contains(runtime_source, "Logger::Error()"));
            EXPECT_FALSE(Contains(runtime_source, ".what()"));
            EXPECT_FALSE(Contains(runtime_source, "result.error().message"));
            EXPECT_FALSE(Contains(runtime_source, "instance_id="));
            EXPECT_FALSE(Contains(runtime_source, "m_instance_id"));
            EXPECT_FALSE(Contains(header, "m_instance_id"));
        }

        TEST(ScheduledTasksTest, BuildsSixBoundedFirstPageJobsForUtcWindows) {
            const auto now = MakeUtcTimePoint(
                std::chrono::year(2026) / std::chrono::July / 19,
                23h,
                59min
            );

            auto plan = BuildPeriodicSeedPlan(now);
            ASSERT_TRUE(plan.has_value()) << plan.error();
            EXPECT_EQ(plan->hourly_scan_id, "20260719T23Z");
            EXPECT_EQ(plan->daily_scan_id, "20260719");
            ASSERT_EQ(plan->jobs.size(), 6U);

            EXPECT_EQ(plan->jobs[0].job_type, disk::jobs::kExpireUploadsJobType);
            EXPECT_EQ(
                plan->jobs[0].dedupe_key,
                "periodic:expire-uploads:20260719T23Z:0"
            );
            EXPECT_EQ(plan->jobs[1].job_type, disk::jobs::kExpireTrashJobType);
            EXPECT_EQ(
                plan->jobs[1].dedupe_key,
                "periodic:expire-trash:20260719T23Z:0"
            );

            constexpr std::array expected_scopes{
                std::string_view("contents"),
                std::string_view("users"),
                std::string_view("staging"),
                std::string_view("final"),
            };
            for (size_t index = 0; index < expected_scopes.size(); index++) {
                const auto& job = plan->jobs[index + 2];
                EXPECT_EQ(job.job_type, disk::jobs::kStorageReconcileJobType);
                EXPECT_EQ(job.aggregate_id, "20260719");
                EXPECT_EQ(job.payload["scope"].asString(), expected_scopes[index]);
                EXPECT_EQ(job.payload["after_id"].asUInt64(), 0U);
                EXPECT_TRUE(job.payload["continuation_token"].asString().empty());
                EXPECT_EQ(
                    job.payload["limit"].asUInt64(),
                    index < 2 ? disk::reconciliation::kMaxDatabaseReconciliationPageSize : disk::reconciliation::kMaxObjectReconciliationPageSize
                );
            }
        }

        TEST(ScheduledTasksTest, RollsHourlyAndDailyIdentitiesAtUtcMidnight) {
            auto before = BuildPeriodicSeedPlan(MakeUtcTimePoint(
                std::chrono::year(2026) / std::chrono::December / 31,
                23h,
                59min
            ));
            auto after = BuildPeriodicSeedPlan(MakeUtcTimePoint(
                std::chrono::year(2027) / std::chrono::January / 1,
                0h
            ));

            ASSERT_TRUE(before.has_value());
            ASSERT_TRUE(after.has_value());
            EXPECT_EQ(before->hourly_scan_id, "20261231T23Z");
            EXPECT_EQ(before->daily_scan_id, "20261231");
            EXPECT_EQ(after->hourly_scan_id, "20270101T00Z");
            EXPECT_EQ(after->daily_scan_id, "20270101");
            EXPECT_NE(before->jobs[0].dedupe_key, after->jobs[0].dedupe_key);
            EXPECT_NE(before->jobs[2].dedupe_key, after->jobs[2].dedupe_key);
        }

        TEST(ScheduledTasksTest, RebuildsStableDedupeKeysWithinSameUtcWindow) {
            auto first = BuildPeriodicSeedPlan(MakeUtcTimePoint(
                std::chrono::year(2026) / std::chrono::July / 19,
                12h,
                1min
            ));
            auto retry = BuildPeriodicSeedPlan(MakeUtcTimePoint(
                std::chrono::year(2026) / std::chrono::July / 19,
                12h,
                58min
            ));

            ASSERT_TRUE(first.has_value());
            ASSERT_TRUE(retry.has_value());
            ASSERT_EQ(first->jobs.size(), retry->jobs.size());
            for (size_t index = 0; index < first->jobs.size(); index++) {
                EXPECT_EQ(first->jobs[index].dedupe_key, retry->jobs[index].dedupe_key);
                EXPECT_EQ(first->jobs[index].payload, retry->jobs[index].payload);
            }
        }
    } // namespace
} // namespace disk::services
