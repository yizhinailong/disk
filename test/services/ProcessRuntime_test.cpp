#include "services/ProcessRuntime.hpp"

#include <stdexcept>

#include <gtest/gtest.h>

namespace disk::runtime {
    namespace {
        TEST(ProcessRuntimeTest, RecognizesOnlyDocumentedHealthPaths) {
            EXPECT_TRUE(IsHealthProbePath("/api/health"));
            EXPECT_TRUE(IsHealthProbePath("/api/health/live"));
            EXPECT_TRUE(IsHealthProbePath("/api/health/ready"));
            EXPECT_FALSE(IsHealthProbePath("/api/health/ready/extra"));
            EXPECT_FALSE(IsHealthProbePath("/api/system/info"));
        }

        TEST(ProcessRuntimeTest, OperationalPathsIncludeHealthAndInternalMetrics) {
            EXPECT_TRUE(IsInternalOperationalPath("/api/health"));
            EXPECT_TRUE(IsInternalOperationalPath("/metrics"));
            EXPECT_FALSE(IsInternalOperationalPath("/metrics/extra"));
            EXPECT_FALSE(IsInternalOperationalPath("/api/file/list"));
        }

        TEST(ProcessRuntimeTest, ApiAcceptsOnlyAfterInitializationAndBeforeDrain) {
            ProcessRuntimeState state(disk::utils::ProcessRole::Api, "api-1");

            EXPECT_FALSE(state.IsReady());
            EXPECT_FALSE(state.TryAcquireBusinessRequest());

            state.MarkInitialized();
            EXPECT_TRUE(state.IsReady());
            EXPECT_TRUE(state.TryAcquireBusinessRequest());
            EXPECT_EQ(state.BusinessRequestsInflight(), 1U);

            EXPECT_TRUE(state.BeginDrain());
            EXPECT_FALSE(state.BeginDrain());
            EXPECT_FALSE(state.IsReady());
            EXPECT_FALSE(state.TryAcquireBusinessRequest());
            EXPECT_FALSE(state.IsApiDrained());

            state.ReleaseBusinessRequest();
            EXPECT_TRUE(state.IsApiDrained());
            state.ReleaseBusinessRequest();
            EXPECT_EQ(state.BusinessRequestsInflight(), 0U);
        }

        TEST(ProcessRuntimeTest, WorkerReadinessRequiresClaimingButRejectsBusiness) {
            ProcessRuntimeState state(disk::utils::ProcessRole::Worker, "worker-1");
            state.MarkInitialized();

            EXPECT_TRUE(state.IsWorkerClaimingEnabled());
            EXPECT_FALSE(state.IsReady());
            EXPECT_FALSE(state.TryAcquireBusinessRequest());

            state.SetWorkerAccepting(true);
            EXPECT_TRUE(state.IsReady());
            EXPECT_TRUE(state.IsWorkerAccepting());
            EXPECT_FALSE(state.TryAcquireBusinessRequest());

            static_cast<void>(state.BeginDrain());
            state.SetWorkerAccepting(true);
            EXPECT_FALSE(state.IsWorkerAccepting());
            EXPECT_FALSE(state.IsReady());
        }

        TEST(ProcessRuntimeTest, ObservationWorkerIsReadyWithoutAcceptingJobs) {
            ProcessRuntimeState state(
                disk::utils::ProcessRole::Worker,
                "worker-observer-1",
                false
            );
            state.MarkInitialized();

            EXPECT_FALSE(state.IsWorkerClaimingEnabled());
            EXPECT_FALSE(state.IsWorkerAccepting());
            EXPECT_TRUE(state.IsReady());
            EXPECT_FALSE(state.TryAcquireBusinessRequest());

            state.SetWorkerAccepting(true);
            EXPECT_FALSE(state.IsWorkerAccepting());
            EXPECT_TRUE(state.IsReady());

            static_cast<void>(state.BeginDrain());
            EXPECT_FALSE(state.IsReady());
        }

        TEST(ProcessRuntimeTest, ApiNeverReportsWorkerClaimingEnabled) {
            ProcessRuntimeState state(disk::utils::ProcessRole::Api, "api-1", true);
            state.MarkInitialized();

            EXPECT_FALSE(state.IsWorkerClaimingEnabled());
            EXPECT_FALSE(state.IsWorkerAccepting());
            EXPECT_TRUE(state.IsReady());
        }

        TEST(ProcessRuntimeTest, UploadCreationSettingIsFrozenAtStartup) {
            ProcessRuntimeState enabled(
                disk::utils::ProcessRole::Api,
                "api-enabled",
                true,
                true
            );
            ProcessRuntimeState disabled(
                disk::utils::ProcessRole::Api,
                "api-disabled",
                true,
                false
            );

            enabled.MarkInitialized();
            disabled.MarkInitialized();

            EXPECT_TRUE(enabled.IsUploadTaskCreationEnabled());
            EXPECT_FALSE(disabled.IsUploadTaskCreationEnabled());
            EXPECT_TRUE(enabled.IsReady());
            EXPECT_TRUE(disabled.IsReady());
        }

        TEST(ProcessRuntimeTest, AllRoleRequiresWorkerAndApiSides) {
            ProcessRuntimeState state(disk::utils::ProcessRole::All, "all-1");
            state.MarkInitialized();
            EXPECT_FALSE(state.IsReady());

            state.SetWorkerAccepting(true);
            EXPECT_TRUE(state.IsReady());
            EXPECT_TRUE(state.TryAcquireBusinessRequest());
            state.ReleaseBusinessRequest();
        }

        TEST(ProcessRuntimeTest, ValidatesInstanceId) {
            EXPECT_THROW(
                ProcessRuntimeState(disk::utils::ProcessRole::Api, ""),
                std::invalid_argument
            );
            EXPECT_EQ(
                ProcessRuntimeState(disk::utils::ProcessRole::Api, "api-1").InstanceId(),
                "api-1"
            );
        }
    } // namespace
} // namespace disk::runtime
