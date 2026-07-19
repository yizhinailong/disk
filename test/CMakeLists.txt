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
    services/HashUtil_test.cpp
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
    services/UploadTaskRepository_test.cpp
    services/FileRepository_test.cpp
    services/FolderRepository_test.cpp
    services/FileMutationServiceMove_test.cpp
    services/FileServiceAtomicity_test.cpp
    services/FileServiceBatch_test.cpp
    services/FileServiceSearch_test.cpp
    services/ShareService_test.cpp
    services/ShareServiceBatch_test.cpp
    services/ShareServiceQuery_test.cpp
    services/ShareAuditService_test.cpp
    services/TokenService_test.cpp
    services/TokenServiceRevocation_test.cpp
    services/TrashService_test.cpp
    services/TrashQuery_test.cpp
    services/TrashServiceBatch_test.cpp
    services/ParallelDeletePaths_test.cpp
    services/UserService_test.cpp
    services/CleanupService_test.cpp
    services/SystemService_test.cpp
    services/OperationLogJsonDetails_test.cpp
    services/OperationLogService_test.cpp
    filters/AdminAuthFilter_test.cpp
    filters/ShareAuthFilter_test.cpp
    filters/ShareRateLimitFilter_test.cpp
    filters/JwtAuthFilter_test.cpp
    filters/RegisterRateLimit_test.cpp
    filters/DownloadRateLimit_test.cpp
    filters/AdminRateLimit_test.cpp
    filters/FolderRateLimit_test.cpp
    filters/FilterOwnership_test.cpp
    dtos/AdminDto_test.cpp
    dtos/AuthDto_test.cpp
    dtos/FileDto_test.cpp
    dtos/FolderDto_test.cpp
    dtos/ShareDto_test.cpp
    dtos/TrashDto_test.cpp
    dtos/UserDto_test.cpp
    storage/AssemblyWorkerPool_test.cpp
    storage/AssemblyPerf_test.cpp
    storage/LocalBlobStore_test.cpp
    storage/S3ObjectStorage_test.cpp
    storage/StorageFactory_test.cpp
    controllers/DownloadResponder_test.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/DownloadResponder.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/BlobStoreMgr.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/LocalBlobStore.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/LocalFileStorage.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/S3ObjectStorage.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/S3Client.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/StorageFactory.cpp
    ${CMAKE_SOURCE_DIR}/src/services/FileRepository.cpp
    ${CMAKE_SOURCE_DIR}/src/services/FolderRepository.cpp
    ${CMAKE_SOURCE_DIR}/src/services/FileServiceUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/services/CleanupService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/ContentService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/QuotaService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/ShareAuditService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/TrashQuery.cpp
    ${CMAKE_SOURCE_DIR}/src/services/TrashService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/UploadLifecycleService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/UploadTaskRepository.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/AdminAuthFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/ShareAuthFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/ShareRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/RegisterRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/DownloadRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/AdminRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/FolderRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/ConfigMgr.cpp
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
    ${CMAKE_SOURCE_DIR}/src/services/CleanupService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/UploadLifecycleService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/UploadTaskRepository.cpp
    ${CMAKE_SOURCE_DIR}/src/services/TrashQuery.cpp
    ${CMAKE_SOURCE_DIR}/src/services/TrashService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/QuotaService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/ContentService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/TokenService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/RedisService.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/AdminAuthFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/ShareAuthFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/ShareRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/RegisterRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/DownloadRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/AdminRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/FolderRateLimitFilter.cpp
    filters/AdminAuthFilter_test.cpp
    filters/ShareRateLimitFilter_test.cpp
    filters/RegisterRateLimit_test.cpp
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
