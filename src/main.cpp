#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <drogon/drogon.h>

#include "application/ApplicationContext.hpp"
#include "filters/RequestTraceFilter.hpp"
#include "services/MetricsService.hpp"
#include "services/MultipartUploadJournal.hpp"
#include "services/ObservedDbClient.hpp"
#include "services/ProcessRuntime.hpp"
#include "services/RedisService.hpp"
#include "services/ScheduledTasks.hpp"
#include "services/StorageJobWorker.hpp"
#include "services/StorageWorkerRuntime.hpp"
#include "services/TokenService.hpp"
#include "storage/BlobStoreMgr.hpp"
#include "storage/MultipartUploadRecovery.hpp"
#include "storage/S3ObjectStorage.hpp"
#include "storage/StorageFactory.hpp"
#include "storage/StorageMgr.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/LogHelper.hpp"
#include "utils/Response.hpp"
#include "utils/RuntimeConfig.hpp"

namespace {
    constexpr std::string_view kBusinessRequestMarker = "runtime_business_request";
    constexpr std::string_view kRequestStartMarker = "runtime_request_started_at";
    constexpr std::string_view kRequestOperationMarker = "runtime_request_operation";

    using BusinessRequestMarker = std::shared_ptr<std::atomic_bool>;

    struct RuntimeServices {
        explicit RuntimeServices(std::shared_ptr<disk::runtime::ProcessRuntimeState> runtime_state)
            : state(std::move(runtime_state)) {}

        std::shared_ptr<disk::runtime::ProcessRuntimeState> state;
        std::atomic<std::shared_ptr<disk::jobs::StorageWorkerRuntime>> worker_runtime;
        std::chrono::steady_clock::time_point shutdown_deadline;
        std::atomic<trantor::TimerId> shutdown_timer{ trantor::InvalidTimerId };
    };

    [[nodiscard]] auto BuildServiceUnavailableResponse() -> drogon::HttpResponsePtr {
        auto response = disk::Response::Fail(
            ErrorCode::InternalError,
            "Service is not accepting new requests"
        );
        response->setStatusCode(drogon::k503ServiceUnavailable);
        response->addHeader("Retry-After", "1");
        return response;
    }

    auto RegisterRequestLifecycle(
        const std::shared_ptr<RuntimeServices>& services
    ) -> void {
        drogon::app().registerPreRoutingAdvice(
            [services](
                const drogon::HttpRequestPtr& request,
                drogon::AdviceCallback&& callback,
                drogon::AdviceChainCallback&& chain_callback
            ) {
                const auto attributes = request->attributes();
                if (!attributes->find("request_id")) {
                    attributes->insert(
                        "request_id",
                        disk::filters::RequestTraceFilter::ResolveRequestId(request)
                    );
                }
                attributes->insert(
                    std::string(kRequestStartMarker),
                    std::chrono::steady_clock::now()
                );
                attributes->insert(
                    std::string(kRequestOperationMarker),
                    disk::metrics::ClassifyHttpOperation(request->path())
                );

                if (disk::runtime::IsInternalOperationalPath(request->path())) {
                    chain_callback();
                    return;
                }
                if (!services->state->TryAcquireBusinessRequest()) {
                    callback(BuildServiceUnavailableResponse());
                    return;
                }

                request->attributes()->insert(
                    std::string(kBusinessRequestMarker),
                    std::make_shared<std::atomic_bool>(false)
                );
                chain_callback();
            }
        );

        drogon::app().registerPreSendingAdvice(
            [services](
                const drogon::HttpRequestPtr& request,
                const drogon::HttpResponsePtr& response
            ) {
                const auto attributes = request->attributes();
                if (attributes->find(std::string(kBusinessRequestMarker))) {
                    const auto& marker = attributes->get<BusinessRequestMarker>(
                        std::string(kBusinessRequestMarker)
                    );
                    if (marker != nullptr && !marker->exchange(true)) {
                        services->state->ReleaseBusinessRequest();
                    }
                }
                if (attributes->find("request_id")) {
                    response->addHeader(
                        "X-Request-Id",
                        attributes->get<std::string>("request_id")
                    );
                }
                response->addHeader("X-Disk-Instance-Id", services->state->InstanceId());

                if (attributes->find(std::string(kRequestStartMarker)) &&
                    attributes->find(std::string(kRequestOperationMarker))) {
                    const auto started_at = attributes->get<std::chrono::steady_clock::time_point>(
                        std::string(kRequestStartMarker)
                    );
                    const auto operation = attributes->get<disk::metrics::HttpOperation>(
                        std::string(kRequestOperationMarker)
                    );
                    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - started_at
                    );
                    const auto status_code = static_cast<int>(response->getStatusCode());
                    disk::metrics::MetricsRegistry::GetInstance().RecordHttpRequest(
                        operation,
                        status_code,
                        duration
                    );

                    const auto request_id = attributes->find("request_id") ?
                                                std::optional<std::string>(
                                                    attributes->get<std::string>("request_id")
                                                ) :
                                                std::nullopt;
                    const disk::utils::LogContext log_context{
                        .request_id = request_id,
                        .operation = std::string(
                            disk::metrics::HttpOperationName(operation)
                        ),
                    };
                    if (status_code >= 400) {
                        disk::utils::Logger::Warn(log_context)
                            << "HTTP request completed: request_id="
                            << request_id.value_or("missing")
                            << ", instance_id=" << services->state->InstanceId()
                            << ", operation=" << disk::metrics::HttpOperationName(operation)
                            << ", status=" << status_code
                            << ", duration_us=" << duration.count();
                    } else {
                        disk::utils::Logger::Debug(log_context)
                            << "HTTP request completed: request_id="
                            << request_id.value_or("missing")
                            << ", instance_id=" << services->state->InstanceId()
                            << ", operation=" << disk::metrics::HttpOperationName(operation)
                            << ", status=" << status_code
                            << ", duration_us=" << duration.count();
                    }
                }
            }
        );
    }

    auto BeginShutdown(
        const std::shared_ptr<RuntimeServices>& services,
        uint32_t drain_timeout_seconds
    ) -> void {
        if (!services->state->BeginDrain()) {
            return;
        }

        const auto worker_runtime = services->worker_runtime.load();
        if (worker_runtime != nullptr) {
            worker_runtime->BeginDrain();
            disk::services::ScheduledTasks::Stop();
        }
        services->shutdown_deadline = std::chrono::steady_clock::now() +
                                      std::chrono::seconds(drain_timeout_seconds);

        disk::utils::Logger::Info(
            disk::utils::LogContext{ .operation = "process_runtime" }
        ) << "Process draining: role="
          << disk::utils::ProcessRoleName(services->state->Role())
          << ", timeout_seconds=" << drain_timeout_seconds;

        auto* loop = drogon::app().getLoop();
        if (loop == nullptr) {
            drogon::app().quit();
            return;
        }

        const auto check_drained = [services, loop]() {
            const auto api_drained = services->state->IsApiDrained();
            const auto worker_runtime = services->worker_runtime.load();
            const auto worker_drained = worker_runtime == nullptr || worker_runtime->IsDrained();
            const auto scheduler_drained = worker_runtime == nullptr ||
                                           disk::services::ScheduledTasks::GetInstance()
                                               ->IsDrained();
            const auto timed_out =
                std::chrono::steady_clock::now() >= services->shutdown_deadline;
            if (!timed_out && (!api_drained || !worker_drained || !scheduler_drained)) {
                return;
            }

            if (timed_out && (!api_drained || !worker_drained || !scheduler_drained)) {
                disk::utils::Logger::Warn(
                    disk::utils::LogContext{ .operation = "process_runtime" }
                ) << "Process drain deadline reached: api_inflight="
                  << services->state->BusinessRequestsInflight()
                  << ", worker_drained=" << worker_drained
                  << ", scheduler_drained=" << scheduler_drained;
            } else {
                disk::utils::Logger::Info(
                    disk::utils::LogContext{ .operation = "process_runtime" }
                ) << "Process drain completed";
            }

            const auto timer = services->shutdown_timer.exchange(trantor::InvalidTimerId);
            if (timer != trantor::InvalidTimerId) {
                loop->invalidateTimer(timer);
            }
            drogon::app().quit();
        };

        services->shutdown_timer.store(loop->runEvery(0.1, check_drained));
        loop->queueInLoop(check_drained);
    }
} // namespace

auto main() -> int {
    disk::utils::Logger::Init();
    disk::utils::Logger::CaptureFrameworkLogs();
    const disk::utils::LogContext bootstrap_log_context{ .operation = "process_bootstrap" };

    disk::utils::Logger::Info(bootstrap_log_context) << "Disk system starting...";

    /// 初始化 libsodium 加密库
    if (sodium_init() < 0) {
        disk::utils::Logger::Error(bootstrap_log_context) << "libsodium initialization failed";
        return 1;
    }
    disk::utils::Logger::Info(bootstrap_log_context) << "libsodium initialized successfully";

    /// 加载配置文件并在交给 Drogon 前应用严格的环境覆盖。
    try {
        drogon::app().loadConfigJson(disk::utils::RuntimeConfig::LoadFromEnvironment());
    } catch (const std::exception&) {
        disk::utils::Logger::Error(bootstrap_log_context)
            << "Runtime configuration loading failed";
        return 1;
    }
    disk::utils::Logger::Info(bootstrap_log_context)
        << "Runtime configuration loaded successfully";

    /// 使用 config.json 中的值初始化 ConfigMgr
    disk::utils::ConfigMgr::GetInstance()->LoadConfig();

    /// 验证配置（JWT_SECRET 所有环境必须设置，DATABASE/REDIS 仅安全模式要求）
    try {
        disk::utils::ConfigMgr::GetInstance()->ValidateSecureConfig();
    } catch (const std::runtime_error&) {
        disk::utils::Logger::Error(bootstrap_log_context)
            << "Secure config validation failed";
        return 1;
    }

    const auto config = disk::utils::ConfigMgr::GetInstance();
    const auto role = config->GetProcessRole();
    disk::utils::Logger::SetInstanceId(config->GetInstanceId());
    auto runtime_services = std::make_shared<RuntimeServices>(
        std::make_shared<disk::runtime::ProcessRuntimeState>(
            role,
            config->GetInstanceId(),
            config->GetWorkerClaimingEnabled(),
            config->GetUploadTaskCreationEnabled()
        )
    );
    disk::runtime::ProcessRuntimeMgr::SetInstance(runtime_services->state);

    if (disk::utils::IncludesApi(role)) {
        disk::services::TokenService::Initialize(config->GetJwtSecret());
        disk::utils::Logger::Info() << "TokenService initialized successfully";
    }

    /// 记录有效存储路径
    disk::utils::Logger::Info() << "Effective storage configuration:";
    disk::utils::Logger::Info() << "  storage_base_path: "
                                << config->GetStorageBasePath();
    disk::utils::Logger::Info() << "  temp_upload_path: " << config->GetTempUploadPath();
    disk::utils::Logger::Info() << "  chunk_size: " << config->GetChunkSize();
    disk::utils::Logger::Info() << "  max_file_size: " << config->GetMaxFileSize();
    disk::utils::Logger::Info() << "  upload_task_expiry_seconds: "
                                << config->GetUploadTaskExpirySeconds();
    disk::utils::Logger::Info() << "  assembly_max_concurrent: "
                                << config->GetAssemblyMaxConcurrent();
    disk::utils::Logger::Info() << "  assemble_buffer_size_bytes: "
                                << config->GetAssembleBufferSizeBytes();

    /// 初始化文件存储和最终 Blob 存储
    try {
        auto storage_bundle = disk::storage::StorageFactory::Create(config);
        disk::storage::StorageMgr::SetInstance(std::move(storage_bundle.storage));
        disk::storage::BlobStoreMgr::SetInstance(std::move(storage_bundle.blob_store));
    } catch (const std::runtime_error& e) {
        disk::utils::Logger::Error() << "File storage initialization failed: " << e.what();
        return 1;
    }
    disk::utils::Logger::Info() << "File storage initialized successfully";

    disk::utils::Logger::Info() << "Drogon framework version: " << drogon::getVersion();
    disk::utils::Logger::Info() << "Process configured: instance_id=" << config->GetInstanceId()
                                << ", role=" << disk::utils::ProcessRoleName(role);

    RegisterRequestLifecycle(runtime_services);

    drogon::app().registerBeginningAdvice([runtime_services, config, role]() {
        auto db_client = disk::metrics::ObserveDbClient(drogon::app().getDbClient());

        if (disk::utils::IncludesApi(role)) {
            auto redis_client = drogon::app().getRedisClient();
            if (auto* s3_storage = dynamic_cast<disk::storage::S3ObjectStorage*>(
                    disk::storage::StorageMgr::GetStorage()
                );
                s3_storage != nullptr) {
                s3_storage->SetMultipartUploadJournal(
                    std::make_shared<disk::jobs::PostgresMultipartUploadJournal>(
                        db_client,
                        config->GetInstanceId(),
                        config->GetUploadFinalizeLeaseSeconds()
                    )
                );
                disk::utils::Logger::Info() << "S3 multipart recovery journal initialized";
            }

            disk::services::RedisService::Initialize(redis_client);
            disk::application::ApplicationContext::Initialize(
                db_client,
                redis_client,
                disk::storage::StorageMgr::GetStorage(),
                disk::storage::BlobStoreMgr::GetBlobStore(),
                config->GetJwtSecret()
            );
            disk::services::TokenService::GetInstance()->StartCacheMaintenance();
            disk::utils::Logger::Info()
                << "Application service context initialized: instance_id="
                << config->GetInstanceId();
        }

        if (runtime_services->state->IsWorkerClaimingEnabled()) {
            auto worker = std::make_shared<disk::jobs::StorageJobWorker>(
                db_client,
                disk::storage::StorageMgr::GetUploadStagingStorage(),
                disk::storage::BlobStoreMgr::GetBlobStore(),
                config->GetInstanceId(),
                disk::jobs::StorageJobWorkerOptions{
                    .batch_size = disk::jobs::EffectiveWorkerClaimBatchSize(
                        config->GetWorkerClaimBatchSize(),
                        config->GetWorkerConcurrency()
                    ),
                    .lease_duration_seconds = config->GetWorkerLeaseDurationSeconds(),
                },
                dynamic_cast<disk::storage::IMultipartUploadCleaner*>(
                    disk::storage::StorageMgr::GetStorage()
                )
            );
            auto worker_runtime = std::make_shared<disk::jobs::StorageWorkerRuntime>(
                config->GetInstanceId(),
                [worker]() -> drogon::Task<Result<disk::jobs::StorageJobRunResult>> {
                    co_return co_await worker->RunOnce();
                },
                disk::jobs::StorageWorkerRuntimeOptions{
                    .poll_interval_ms = config->GetWorkerPollIntervalMs(),
                }
            );

            disk::services::ScheduledTasks::Initialize(db_client, config->GetInstanceId());
            disk::services::ScheduledTasks::Register();
            worker_runtime->Start(drogon::app().getLoop());
            runtime_services->worker_runtime.store(std::move(worker_runtime));
            runtime_services->state->SetWorkerAccepting(true);
        } else if (disk::utils::IncludesWorker(role)) {
            disk::utils::Logger::Info()
                << "Worker observation mode enabled; job claiming and scheduled task "
                << "registration are disabled: instance_id="
                << config->GetInstanceId();
        }

        runtime_services->state->MarkInitialized();
        disk::utils::Logger::Info() << "Process initialization completed: instance_id="
                                    << config->GetInstanceId()
                                    << ", role=" << disk::utils::ProcessRoleName(role);
    });

    const auto shutdown = [runtime_services, config]() {
        BeginShutdown(runtime_services, config->GetWorkerDrainTimeoutSeconds());
    };
    drogon::app().setTermSignalHandler(shutdown);
    drogon::app().setIntSignalHandler(shutdown);

    drogon::app().run();

    return 0;
}
