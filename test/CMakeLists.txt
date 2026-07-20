enable_testing()

include(GoogleTest)

set(TEST_SOURCES
    main.cpp
    utils/ConfigMgr_test.cpp
    utils/FileHashUtil_test.cpp
    utils/NameValidation_test.cpp
    utils/PasswdHash_test.cpp
    utils/RedisKeyPrefix_test.cpp
    utils/Response_test.cpp
    utils/RuntimeConfig_test.cpp
    services/HashUtil_test.cpp
    services/HealthService_test.cpp
    services/MetricsService_test.cpp
    services/ObservedDbClient_test.cpp
    services/RedisService_test.cpp
    services/LoginRateLimit_test.cpp
    services/ModelDialect_test.cpp
    services/ContentService_test.cpp
    services/QuotaService_test.cpp
    services/TransactionRunner_test.cpp
    services/FileUploadConsistency_test.cpp
    services/UploadPath_test.cpp
    services/CompleteUploadSingleflight_test.cpp
    services/UploadLifecycleService_test.cpp
    services/UploadStateMachine_test.cpp
    services/UploadTaskRepository_test.cpp
    services/FileRepository_test.cpp
    services/FolderRepository_test.cpp
    services/FileListCache_test.cpp
    services/FileMutationServiceMove_test.cpp
    services/FileServiceAtomicity_test.cpp
    services/FileServiceBatch_test.cpp
    services/FileServiceSearch_test.cpp
    services/ShareService_test.cpp
    services/ShareServiceBatch_test.cpp
    services/ShareServiceQuery_test.cpp
    services/ShareAuditService_test.cpp
    services/ScheduledTasks_test.cpp
    services/StorageJobContract_test.cpp
    services/StorageJobRepository_test.cpp
    services/StorageJobWorker_test.cpp
    services/StorageWorkerRuntime_test.cpp
    services/StorageReconciliationService_test.cpp
    services/TokenService_test.cpp
    services/TokenServiceRevocation_test.cpp
    services/TrashService_test.cpp
    services/TrashQuery_test.cpp
    services/TrashServiceBatch_test.cpp
    services/UserService_test.cpp
    services/CleanupService_test.cpp
    services/SystemService_test.cpp
    services/OperationLogJsonDetails_test.cpp
    services/OperationLogService_test.cpp
    services/ProcessRuntime_test.cpp
    filters/AdminAuthFilter_test.cpp
    filters/ShareAuthFilter_test.cpp
    filters/ShareRateLimitFilter_test.cpp
    filters/JwtAuthFilter_test.cpp
    filters/RegisterRateLimit_test.cpp
    filters/RequestTraceFilter_test.cpp
    filters/DownloadRateLimit_test.cpp
    filters/AdminRateLimit_test.cpp
    filters/FolderRateLimit_test.cpp
    filters/FilterOwnership_test.cpp
    dtos/AdminDto_test.cpp
    dtos/AuthDto_test.cpp
    dtos/FileDto_test.cpp
    dtos/FolderDto_test.cpp
    dtos/ShareDto_test.cpp
    dtos/StorageJobAdminDto_test.cpp
    dtos/StorageRecoveryAdminDto_test.cpp
    dtos/TrashDto_test.cpp
    dtos/UploadDiagnosticDto_test.cpp
    dtos/UserDto_test.cpp
    storage/AssemblyWorkerPool_test.cpp
    storage/AssemblyPerf_test.cpp
    storage/LocalBlobStore_test.cpp
    storage/S3Client_test.cpp
    storage/S3ObjectStorage_test.cpp
    storage/StorageFactory_test.cpp
    storage/StorageInventory_test.cpp
    controllers/DownloadResponder_test.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/DownloadResponder.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/BlobStoreMgr.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/LocalBlobStore.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/LocalFileStorage.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/MultipartUploadRecovery.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/S3ObjectStorage.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/S3Client.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/StorageFactory.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/StorageInventory.cpp
    ${CMAKE_SOURCE_DIR}/src/services/FileRepository.cpp
    ${CMAKE_SOURCE_DIR}/src/services/FolderRepository.cpp
    ${CMAKE_SOURCE_DIR}/src/services/FileServiceUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/services/FileListCache.cpp
    ${CMAKE_SOURCE_DIR}/src/services/HealthService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/MetricsService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/ObservedDbClient.cpp
    ${CMAKE_SOURCE_DIR}/src/services/MultipartUploadJournal.cpp
    ${CMAKE_SOURCE_DIR}/src/services/ProcessRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/services/CleanupService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/ContentService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/QuotaService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/ShareAuditService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/ScheduledTasks.cpp
    ${CMAKE_SOURCE_DIR}/src/services/StorageJobContract.cpp
    ${CMAKE_SOURCE_DIR}/src/services/StorageJobAdminService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/StorageJobRepository.cpp
    ${CMAKE_SOURCE_DIR}/src/services/StorageJobWorker.cpp
    ${CMAKE_SOURCE_DIR}/src/services/StorageWorkerRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/services/StorageReconciliationService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/TrashQuery.cpp
    ${CMAKE_SOURCE_DIR}/src/services/TrashService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/UploadLifecycleService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/UploadStateMachine.cpp
    ${CMAKE_SOURCE_DIR}/src/services/UploadTaskRepository.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/AdminAuthFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/ShareAuthFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/ShareRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/RegisterRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/RequestTraceFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/DownloadRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/AdminRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/FolderRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/ConfigMgr.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/RuntimeConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/StorageMgr.cpp
    ${CMAKE_SOURCE_DIR}/src/models/FileContents.cpp
    ${CMAKE_SOURCE_DIR}/src/models/Files.cpp
    ${CMAKE_SOURCE_DIR}/src/models/Folders.cpp
    ${CMAKE_SOURCE_DIR}/src/models/Trash.cpp
    ${CMAKE_SOURCE_DIR}/src/models/UploadTasks.cpp
    ${CMAKE_SOURCE_DIR}/src/services/TokenService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/RedisService.cpp
)

add_executable(disk-test ${TEST_SOURCES})

# Source files from the main target lack its PCH. Force-include Drogon + LogHelper
# so that all symbols and the Log class are available without the precompiled header.
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/utils/ConfigMgr.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/StorageMgr.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/DownloadResponder.cpp
    ${CMAKE_SOURCE_DIR}/src/services/FileRepository.cpp
    ${CMAKE_SOURCE_DIR}/src/services/FolderRepository.cpp
    ${CMAKE_SOURCE_DIR}/src/services/FileServiceUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/services/FileListCache.cpp
    ${CMAKE_SOURCE_DIR}/src/services/MultipartUploadJournal.cpp
    ${CMAKE_SOURCE_DIR}/src/services/CleanupService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/UploadLifecycleService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/UploadTaskRepository.cpp
    ${CMAKE_SOURCE_DIR}/src/services/TrashQuery.cpp
    ${CMAKE_SOURCE_DIR}/src/services/TrashService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/QuotaService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/ScheduledTasks.cpp
    ${CMAKE_SOURCE_DIR}/src/services/MetricsService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/StorageJobAdminService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/StorageJobRepository.cpp
    ${CMAKE_SOURCE_DIR}/src/services/StorageJobWorker.cpp
    ${CMAKE_SOURCE_DIR}/src/services/ContentService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/TokenService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/RedisService.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/AdminAuthFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/ShareAuthFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/ShareRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/RegisterRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/RequestTraceFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/DownloadRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/AdminRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/FolderRateLimitFilter.cpp
    filters/AdminAuthFilter_test.cpp
    filters/ShareRateLimitFilter_test.cpp
    filters/RegisterRateLimit_test.cpp
    filters/RequestTraceFilter_test.cpp
    filters/DownloadRateLimit_test.cpp
    filters/AdminRateLimit_test.cpp
    filters/FolderRateLimit_test.cpp
    PROPERTIES COMPILE_FLAGS "-include drogon/HttpAppFramework.h -include utils/LogHelper.hpp"
)

target_include_directories(disk-test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/test
    ${AWSSDK_INCLUDE_DIRS}
)

target_link_libraries(disk-test
    PRIVATE
    Drogon::Drogon
    jwt-cpp::jwt-cpp
    unofficial-sodium::sodium
    spdlog::spdlog
    ${AWSSDK_LIBRARIES}
    GTest::gtest
    GTest::gtest_main
)

gtest_discover_tests(disk-test)

function(add_disk_server_integration_test test_name script_name timeout_seconds)
    set(script_path "${CMAKE_SOURCE_DIR}/test/integration/${script_name}")
    file(READ "${script_path}" script_source)
    if(NOT script_source MATCHES "ensure_server[ \t\r\n]*\\(")
        message(FATAL_ERROR "${script_name} must call ensure_server() before CTest registration")
    endif()

    add_test(NAME ${test_name} COMMAND uv run "${script_path}")
    set_tests_properties(
        ${test_name}
        PROPERTIES
        LABELS backend-integration
        RUN_SERIAL TRUE
        TIMEOUT ${timeout_seconds}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endfunction()

function(add_disk_gated_integration_test test_name script_name timeout_seconds skip_pattern)
    add_test(
        NAME ${test_name}
        COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/${script_name}"
    )
    set_tests_properties(
        ${test_name}
        PROPERTIES
        LABELS "backend-integration;environment-gated"
        RUN_SERIAL TRUE
        SKIP_REGULAR_EXPRESSION "${skip_pattern}"
        TIMEOUT ${timeout_seconds}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endfunction()

add_test(
    NAME ServerLifecycleHarness
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_server_lifecycle.py"
)
set_tests_properties(
    ServerLifecycleHarness
    PROPERTIES
    LABELS backend-harness
    TIMEOUT 30
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME V003MigrationIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_v003_migration.py"
)
set_tests_properties(
    V003MigrationIntegration
    PROPERTIES
    LABELS "backend-integration;database-migration"
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME V004ReconciliationMigrationIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_v004_reconciliation_migration.py"
)
set_tests_properties(
    V004ReconciliationMigrationIntegration
    PROPERTIES
    LABELS "backend-integration;database-migration"
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME FullSchemaUpgradeIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_full_schema_upgrade.py"
)
set_tests_properties(
    FullSchemaUpgradeIntegration
    PROPERTIES
    LABELS "backend-integration;database-migration"
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME ExpandMixedVersionIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_expand_mixed_version.py"
)
set_tests_properties(
    ExpandMixedVersionIntegration
    PROPERTIES
    LABELS "backend-integration;database-migration;rolling-upgrade"
    RUN_SERIAL TRUE
    TIMEOUT 600
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME WorkerObservationModeIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_worker_observation_mode.py"
)
set_tests_properties(
    WorkerObservationModeIntegration
    PROPERTIES
    LABELS "backend-integration;distributed;rolling-upgrade;observability"
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME SchedulerRoleCutoverIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_scheduler_role_cutover.py"
)
set_tests_properties(
    SchedulerRoleCutoverIntegration
    PROPERTIES
    LABELS "backend-integration;distributed;rolling-upgrade"
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME StagingCanaryIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_staging_canary.py"
)
set_tests_properties(
    StagingCanaryIntegration
    PROPERTIES
    LABELS "backend-integration;distributed;s3;rolling-upgrade;observability"
    RUN_SERIAL TRUE
    TIMEOUT 180
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME StagingRolloutExpansionIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_staging_rollout_expansion.py"
)
set_tests_properties(
    StagingRolloutExpansionIntegration
    PROPERTIES
    LABELS "backend-integration;distributed;s3;rolling-upgrade;observability"
    RUN_SERIAL TRUE
    TIMEOUT 180
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME LocalStagingMigrationIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_local_staging_migration.py"
)
set_tests_properties(
    LocalStagingMigrationIntegration
    PROPERTIES
    LABELS "backend-integration;database-migration;rolling-upgrade"
    RUN_SERIAL TRUE
    TIMEOUT 600
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME SecureLocalStagingCutoffIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_secure_local_staging_cutoff.py"
)
set_tests_properties(
    SecureLocalStagingCutoffIntegration
    PROPERTIES
    LABELS "backend-integration;distributed;rolling-upgrade;configuration"
    RUN_SERIAL TRUE
    TIMEOUT 30
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME FinalBlobMigrationIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_final_blob_migration.py"
)
set_tests_properties(
    FinalBlobMigrationIntegration
    PROPERTIES
    LABELS "backend-integration;database-migration;object-storage"
    RUN_SERIAL TRUE
    TIMEOUT 300
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME BackupRestoreReconciliationIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_backup_restore_reconciliation.py"
)
set_tests_properties(
    BackupRestoreReconciliationIntegration
    PROPERTIES
    LABELS "backend-integration;database-migration;backup-restore;object-storage"
    RUN_SERIAL TRUE
    TIMEOUT 180
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME UploadStateMachineIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_upload_state_machine.py"
)
set_tests_properties(
    UploadStateMachineIntegration
    PROPERTIES
    LABELS "backend-integration;database-state-machine"
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME StorageJobQueueIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_storage_job_queue.py"
)
set_tests_properties(
    StorageJobQueueIntegration
    PROPERTIES
    LABELS "backend-integration;database-state-machine"
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME BlobGcProtocolIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_blob_gc_protocol.py"
)
set_tests_properties(
    BlobGcProtocolIntegration
    PROPERTIES
    LABELS "backend-integration;database-state-machine"
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME BlobGcProcessDeathIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_blob_gc_process_death.py"
)
set_tests_properties(
    BlobGcProcessDeathIntegration
    PROPERTIES
    LABELS "backend-integration;database-state-machine;process-death"
    RUN_SERIAL TRUE
    TIMEOUT 150
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME MultipartRecoveryIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_multipart_recovery.py"
)
set_tests_properties(
    MultipartRecoveryIntegration
    PROPERTIES
    LABELS "backend-integration;database-state-machine"
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME S3LifecycleConfigIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_s3_lifecycle_config.py"
)
set_tests_properties(
    S3LifecycleConfigIntegration
    PROPERTIES
    LABELS "backend-integration;object-storage"
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME DistributedTopologyContract
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_distributed_topology.py"
)
set_tests_properties(
    DistributedTopologyContract
    PROPERTIES
    LABELS "backend-integration;distributed;deployment"
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME DistributedCapacityBudgetContract
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_capacity_budget.py"
)
set_tests_properties(
    DistributedCapacityBudgetContract
    PROPERTIES
    LABELS "backend-integration;distributed;deployment"
    TIMEOUT 30
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME ApiPoolRolloutContract
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_api_pool_rollout.py"
)
set_tests_properties(
    ApiPoolRolloutContract
    PROPERTIES
    LABELS "backend-integration;distributed;deployment;rolling-upgrade"
    RUN_SERIAL TRUE
    TIMEOUT 30
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME AlertFaultInjectionIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_alert_fault_injection.py"
)
set_tests_properties(
    AlertFaultInjectionIntegration
    PROPERTIES
    LABELS "backend-integration;distributed;observability"
    RUN_SERIAL TRUE
    TIMEOUT 180
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_disk_gated_integration_test(
    PrometheusAlertRulesIntegration
    test_prometheus_alert_rules.py
    60
    "SKIP: promtool is not available"
)

add_disk_server_integration_test(LoginRateLimitIntegration test_login_rate_limit.py 120)
add_disk_server_integration_test(UserProfileStorageIntegration test_user_profile_storage.py 120)
add_disk_server_integration_test(AssemblyBackpressureIntegration test_assembly_backpressure.py 120)
add_disk_server_integration_test(AuthFlowIntegration test_auth_flow.py 120)
add_disk_server_integration_test(AuthLifecycleIntegration test_auth_lifecycle.py 120)
add_disk_server_integration_test(CopyDeleteAtomicityIntegration test_copy_delete_atomicity.py 120)
add_disk_server_integration_test(DownloadFlowIntegration test_download_flow.py 120)
add_disk_server_integration_test(DomainExtractionInvariantsIntegration test_domain_extraction_invariants.py 180)
add_disk_server_integration_test(FileMetadataQueryIntegration test_file_metadata_query.py 120)
add_disk_server_integration_test(FileMutationOpsIntegration test_file_mutation_ops.py 120)
add_disk_server_integration_test(FolderLifecycleIntegration test_folder_lifecycle.py 120)
add_disk_server_integration_test(HealthAndLogsIntegration test_health_and_logs.py 120)
add_disk_server_integration_test(StorageJobOperationsIntegration test_storage_job_operations.py 120)
add_disk_server_integration_test(StorageJobFailurePersistenceIntegration test_storage_job_failure_persistence.py 120)
add_disk_server_integration_test(PasswordUpdateIntegration test_password_update.py 120)
add_disk_server_integration_test(RefreshTokenIntegration test_refresh_token.py 120)
add_disk_server_integration_test(ShareBrowseIntegration test_share_browse.py 120)
add_disk_server_integration_test(SharePasswordProtectionIntegration test_share_password_protection.py 180)
add_disk_server_integration_test(ShareRateLimitIntegration test_share_rate_limit.py 300)
add_disk_server_integration_test(ShareManagementIntegration test_share_management.py 120)
add_disk_server_integration_test(ShareAuditIntegration test_share_audit.py 180)
add_disk_server_integration_test(ShareTokenSecurityIntegration test_share_token_security.py 120)
add_disk_server_integration_test(SystemInfoIntegration test_system_info.py 120)
add_disk_server_integration_test(TrashLifecycleIntegration test_trash_lifecycle.py 120)
add_disk_server_integration_test(UploadFlowIntegration test_upload_flow.py 120)
add_disk_server_integration_test(UserProfileUpdateIntegration test_user_profile_update.py 120)
add_disk_server_integration_test(FileDeleteRegressionIntegration test_file_delete_regression.py 120)
add_disk_server_integration_test(BrowseBurstRegressionIntegration test_browse_burst_regression.py 120)
add_disk_server_integration_test(UploadRateLimitIntegration test_upload_rate_limit.py 300)
add_disk_server_integration_test(SafetyUploadInvariantsIntegration test_safety_upload_invariants.py 180)
add_disk_server_integration_test(SafetyContentQuotaIntegration test_safety_content_quota.py 240)
add_disk_server_integration_test(SafetyMoveCopyPathIntegration test_safety_move_copy_path.py 180)

add_disk_gated_integration_test(
    S3StorageAdapterIntegration
    test_s3_storage_adapter.py
    300
    "SKIP: DISK_S3_INTEGRATION is not 1"
)
add_disk_gated_integration_test(
    S3AppFlowIntegration
    test_s3_app_flow.py
    300
    "SKIP: DISK_S3_APP_INTEGRATION is not 1"
)
add_disk_gated_integration_test(
    S3ProvisioningIntegration
    test_s3_provisioning.py
    180
    "SKIP: DISK_S3_PROVISIONING_INTEGRATION is not 1"
)
add_disk_gated_integration_test(
    DistributedFlowIntegration
    test_distributed_flow.py
    900
    "SKIP: DISK_DISTRIBUTED_INTEGRATION is not 1"
)
add_disk_gated_integration_test(
    DistributedLocalFlowIntegration
    test_distributed_local.py
    900
    "SKIP: DISK_DISTRIBUTED_LOCAL_INTEGRATION is not 1"
)
