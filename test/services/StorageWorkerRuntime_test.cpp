#include "services/StorageWorkerRuntime.hpp"

#include <stdexcept>

#include <gtest/gtest.h>

namespace disk::jobs {
    namespace {
        TEST(StorageWorkerRuntimeTest, ValidatesRequiredConfiguration) {
            const auto callback = []() -> drogon::Task<Result<StorageJobRunResult>> {
                co_return StorageJobRunResult{};
            };

            EXPECT_THROW(
                StorageWorkerRuntime("", callback),
                std::invalid_argument
            );
            EXPECT_THROW(
                StorageWorkerRuntime("worker-1", {}),
                std::invalid_argument
            );
            EXPECT_THROW(
                StorageWorkerRuntime(
                    "worker-1",
                    callback,
                    StorageWorkerRuntimeOptions{ .poll_interval_ms = 99 }
                ),
                std::invalid_argument
            );
        }

        TEST(StorageWorkerRuntimeTest, PollsInjectedWorkerAndResetsInflightState) {
            size_t calls = 0;
            StorageWorkerRuntime runtime(
                "worker-1",
                [&calls]() -> drogon::Task<Result<StorageJobRunResult>> {
                    calls++;
                    co_return StorageJobRunResult{
                        .claimed = 2,
                        .succeeded = 1,
                        .retried = 1,
                    };
                }
            );

            EXPECT_TRUE(drogon::sync_wait(runtime.PollOnce()));
            EXPECT_EQ(calls, 1);
            EXPECT_TRUE(runtime.IsDrained());
            EXPECT_TRUE(runtime.IsAccepting());
        }

        TEST(StorageWorkerRuntimeTest, ContainsRunnerFailuresAndCanPollAgain) {
            size_t calls = 0;
            StorageWorkerRuntime runtime(
                "worker-1",
                [&calls]() -> drogon::Task<Result<StorageJobRunResult>> {
                    calls++;
                    if (calls == 1) {
                        throw std::runtime_error("injected failure");
                    }
                    co_return std::unexpected(
                        ErrorInfo(ErrorCode::InternalError, "injected result failure")
                    );
                }
            );

            EXPECT_TRUE(drogon::sync_wait(runtime.PollOnce()));
            EXPECT_TRUE(drogon::sync_wait(runtime.PollOnce()));
            EXPECT_EQ(calls, 2);
            EXPECT_TRUE(runtime.IsDrained());
        }

        TEST(StorageWorkerRuntimeTest, DrainPermanentlyStopsNewClaims) {
            size_t calls = 0;
            StorageWorkerRuntime runtime(
                "worker-1",
                [&calls]() -> drogon::Task<Result<StorageJobRunResult>> {
                    calls++;
                    co_return StorageJobRunResult{};
                }
            );

            runtime.BeginDrain();
            runtime.BeginDrain();

            EXPECT_FALSE(runtime.IsAccepting());
            EXPECT_TRUE(runtime.IsDrained());
            EXPECT_FALSE(drogon::sync_wait(runtime.PollOnce()));
            EXPECT_EQ(calls, 0);
        }
    } // namespace
} // namespace disk::jobs
