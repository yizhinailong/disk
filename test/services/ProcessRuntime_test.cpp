#include "services/ProcessRuntime.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

namespace disk::runtime {
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

        TEST(ProcessDrainLogContextContractTest, UsesTypedProcessCorrelationWithoutOwnership) {
            const auto source = ReadSourceFile("src/main.cpp");
            const auto shutdown_begin = source.find("auto BeginShutdown(");
            const auto shutdown_end = source.find("} // namespace", shutdown_begin);

            ASSERT_NE(shutdown_begin, std::string::npos);
            ASSERT_NE(shutdown_end, std::string::npos);
            const auto shutdown_source =
                source.substr(shutdown_begin, shutdown_end - shutdown_begin);

            EXPECT_EQ(CountOccurrences(shutdown_source, "Logger::Info("), 2U);
            EXPECT_EQ(CountOccurrences(shutdown_source, "Logger::Warn("), 1U);
            EXPECT_EQ(
                CountOccurrences(shutdown_source, ".operation = \"process_runtime\""),
                3U
            );
            EXPECT_TRUE(Contains(shutdown_source, "\"Process draining: role=\""));
            EXPECT_TRUE(Contains(
                shutdown_source,
                "\"Process drain deadline reached: api_inflight=\""
            ));
            EXPECT_TRUE(Contains(shutdown_source, "\"Process drain completed\""));
            EXPECT_FALSE(Contains(shutdown_source, "Logger::Info()"));
            EXPECT_FALSE(Contains(shutdown_source, "Logger::Warn()"));
            EXPECT_FALSE(Contains(shutdown_source, "instance_id="));
            EXPECT_FALSE(Contains(shutdown_source, "InstanceId()"));
            EXPECT_FALSE(Contains(shutdown_source, "request_id="));
            EXPECT_FALSE(Contains(shutdown_source, "upload_id="));
            EXPECT_FALSE(Contains(shutdown_source, "job_id="));
            EXPECT_FALSE(Contains(shutdown_source, "lease_owner="));
            EXPECT_FALSE(Contains(shutdown_source, "state_version="));
        }

        TEST(ProcessBootstrapLogContextContractTest, UsesTypedPreInstanceCorrelation) {
            const auto source = ReadSourceFile("src/main.cpp");
            const auto bootstrap_begin = source.find("auto main() -> int");
            const auto bootstrap_end = source.find("Logger::SetInstanceId", bootstrap_begin);

            ASSERT_NE(bootstrap_begin, std::string::npos);
            ASSERT_NE(bootstrap_end, std::string::npos);
            const auto bootstrap_source =
                source.substr(bootstrap_begin, bootstrap_end - bootstrap_begin);

            EXPECT_EQ(CountOccurrences(bootstrap_source, "Logger::Info(bootstrap_log_context)"), 3U);
            EXPECT_EQ(CountOccurrences(bootstrap_source, "Logger::Error(bootstrap_log_context)"), 3U);
            EXPECT_EQ(
                CountOccurrences(bootstrap_source, ".operation = \"process_bootstrap\""),
                1U
            );
            EXPECT_TRUE(Contains(bootstrap_source, "\"Disk system starting...\""));
            EXPECT_TRUE(Contains(bootstrap_source, "\"libsodium initialization failed\""));
            EXPECT_TRUE(Contains(bootstrap_source, "\"libsodium initialized successfully\""));
            EXPECT_TRUE(Contains(
                bootstrap_source,
                "\"Runtime configuration loading failed\""
            ));
            EXPECT_TRUE(Contains(
                bootstrap_source,
                "\"Runtime configuration loaded successfully\""
            ));
            EXPECT_TRUE(Contains(bootstrap_source, "\"Secure config validation failed\""));
            EXPECT_FALSE(Contains(bootstrap_source, "Logger::Info()"));
            EXPECT_FALSE(Contains(bootstrap_source, "Logger::Error()"));
            EXPECT_FALSE(Contains(bootstrap_source, ".what()"));
            EXPECT_FALSE(Contains(bootstrap_source, "instance_id="));
            EXPECT_FALSE(Contains(bootstrap_source, "request_id="));
            EXPECT_FALSE(Contains(bootstrap_source, "upload_id="));
            EXPECT_FALSE(Contains(bootstrap_source, "job_id="));
            EXPECT_FALSE(Contains(bootstrap_source, "lease_owner="));
            EXPECT_FALSE(Contains(bootstrap_source, "state_version="));
        }

        TEST(ProcessInitializationLogContextContractTest, UsesTypedPostRegistrationCorrelation) {
            const auto source = ReadSourceFile("src/main.cpp");
            const auto initialization_begin = source.find("Logger::SetInstanceId");
            const auto initialization_end = source.find(
                "const auto shutdown =",
                initialization_begin
            );

            ASSERT_NE(initialization_begin, std::string::npos);
            ASSERT_NE(initialization_end, std::string::npos);
            const auto initialization_source = source.substr(
                initialization_begin,
                initialization_end - initialization_begin
            );

            EXPECT_EQ(
                CountOccurrences(
                    initialization_source,
                    "Logger::Info(process_runtime_log_context)"
                ),
                5U
            );
            EXPECT_EQ(
                CountOccurrences(
                    initialization_source,
                    "Logger::Info(disk::storage::StorageRuntimeLogContext())"
                ),
                2U
            );
            EXPECT_EQ(
                CountOccurrences(
                    initialization_source,
                    "Logger::Error(disk::storage::StorageRuntimeLogContext())"
                ),
                1U
            );
            EXPECT_EQ(
                CountOccurrences(initialization_source, ".operation = \"process_runtime\""),
                1U
            );
            EXPECT_TRUE(Contains(initialization_source, "\"Token service initialized\""));
            EXPECT_TRUE(Contains(
                initialization_source,
                "\"Storage manager initialization failed\""
            ));
            EXPECT_TRUE(Contains(initialization_source, "\"Storage managers initialized\""));
            EXPECT_TRUE(Contains(
                initialization_source,
                "\"Process runtime configured: framework_version=\""
            ));
            EXPECT_TRUE(Contains(
                initialization_source,
                "\"S3 multipart recovery journal initialized\""
            ));
            EXPECT_TRUE(Contains(
                initialization_source,
                "\"Application service context initialized\""
            ));
            EXPECT_TRUE(Contains(
                initialization_source,
                "\"Worker observation mode enabled; job claiming and scheduled task \""
            ));
            EXPECT_TRUE(Contains(
                initialization_source,
                "\"Process initialization completed: role=\""
            ));
            EXPECT_FALSE(Contains(initialization_source, "Logger::Info()"));
            EXPECT_FALSE(Contains(initialization_source, "Logger::Error()"));
            EXPECT_FALSE(Contains(initialization_source, ".what()"));
            EXPECT_FALSE(Contains(initialization_source, "instance_id="));
            EXPECT_FALSE(Contains(initialization_source, "GetStorageBasePath()"));
            EXPECT_FALSE(Contains(initialization_source, "GetTempUploadPath()"));
            EXPECT_FALSE(Contains(initialization_source, "GetChunkSize()"));
            EXPECT_FALSE(Contains(initialization_source, "GetMaxFileSize()"));
            EXPECT_FALSE(Contains(initialization_source, "GetUploadTaskExpirySeconds()"));
            EXPECT_FALSE(Contains(initialization_source, "GetAssemblyMaxConcurrent()"));
            EXPECT_FALSE(Contains(initialization_source, "GetAssembleBufferSizeBytes()"));

            const auto log_helper = ReadSourceFile("src/utils/LogHelper.hpp");
            ASSERT_FALSE(log_helper.empty());
            EXPECT_EQ(
                CountOccurrences(log_helper, ".operation = \"service_runtime\""),
                1U
            );

            constexpr std::array<std::pair<std::string_view, std::string_view>, 16> service_logs{
                {
                 { "src/services/AdminService.cpp", "admin" },
                 { "src/services/AuthService.cpp", "auth" },
                 { "src/services/CleanupService.cpp", "cleanup" },
                 { "src/services/ContentService.cpp", "content" },
                 { "src/services/FileMutationService.cpp", "file_mutation" },
                 { "src/services/FileQueryService.cpp", "file_query" },
                 { "src/services/FolderService.cpp", "folder" },
                 { "src/services/OperationLogService.cpp", "operation_log" },
                 { "src/services/QuotaService.cpp", "quota" },
                 { "src/services/RedisService.cpp", "redis" },
                 { "src/services/ShareService.cpp", "share" },
                 { "src/services/SystemService.cpp", "system" },
                 { "src/services/TrashService.cpp", "trash" },
                 { "src/services/UploadLifecycleService.cpp", "upload_lifecycle" },
                 { "src/services/UploadService.cpp", "upload" },
                 { "src/services/UserService.cpp", "user" },
                 }
            };

            std::string combined_service_sources;
            for (const auto& [path, service] : service_logs) {
                const auto service_source = ReadSourceFile(path);
                ASSERT_FALSE(service_source.empty()) << path;
                combined_service_sources += service_source;
                EXPECT_TRUE(Contains(
                    service_source,
                    "Logger::Debug(disk::utils::ServiceRuntimeLogContext()) << \"Service initialized: service=" +
                        std::string(service) + "\""
                )) << path;
            }

            EXPECT_EQ(
                CountOccurrences(
                    combined_service_sources,
                    "Logger::Debug(disk::utils::ServiceRuntimeLogContext())"
                ),
                service_logs.size()
            );
            EXPECT_EQ(
                CountOccurrences(combined_service_sources, "Service initialized: service="),
                service_logs.size()
            );
            EXPECT_EQ(CountOccurrences(combined_service_sources, "Logger::Debug()"), 1U);
            EXPECT_TRUE(Contains(
                combined_service_sources,
                "Upload task cache maintenance timer started"
            ));
            EXPECT_FALSE(Contains(combined_service_sources, "initialization completed"));
            EXPECT_FALSE(Contains(combined_service_sources, "RedisService initialized"));
            EXPECT_FALSE(Contains(combined_service_sources, "instance_id="));
        }

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
