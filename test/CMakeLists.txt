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
    services/ContentService_test.cpp
    services/QuotaService_test.cpp
    services/FileUploadConsistency_test.cpp
    services/UploadPath_test.cpp
    services/CompleteUploadSingleflight_test.cpp
    services/FileServiceAtomicity_test.cpp
    services/FileServiceBatch_test.cpp
    services/FileServiceSearch_test.cpp
    services/ShareService_test.cpp
    services/ShareServiceBatch_test.cpp
    services/ShareServiceQuery_test.cpp
    services/TokenService_test.cpp
    services/TokenServiceRevocation_test.cpp
    services/TrashService_test.cpp
    services/TrashServiceBatch_test.cpp
    services/ParallelDeletePaths_test.cpp
    services/UserService_test.cpp
    services/CleanupService_test.cpp
    services/SystemService_test.cpp
    services/OperationLogJsonDetails_test.cpp
    services/OperationLogService_test.cpp
    filters/AdminAuthFilter_test.cpp
    filters/ShareAuthFilter_test.cpp
    filters/JwtAuthFilter_test.cpp
    filters/RegisterRateLimit_test.cpp
    filters/DownloadRateLimit_test.cpp
    filters/SharePublicRateLimit_test.cpp
    filters/AdminRateLimit_test.cpp
    filters/FolderRateLimit_test.cpp
    dtos/AdminDto_test.cpp
    dtos/AuthDto_test.cpp
    dtos/FileDto_test.cpp
    dtos/FolderDto_test.cpp
    dtos/ShareDto_test.cpp
    dtos/TrashDto_test.cpp
    dtos/UserDto_test.cpp
    storage/AssemblyWorkerPool_test.cpp
    storage/AssemblyPerf_test.cpp
    controllers/DownloadResponder_test.cpp
    ${CMAKE_SOURCE_DIR}/src/controllers/DownloadResponder.cpp
    ${CMAKE_SOURCE_DIR}/src/services/FileServiceUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/services/CleanupService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/ContentService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/QuotaService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/TrashService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/UploadLifecycleService.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/AdminAuthFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/ShareAuthFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/RegisterRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/DownloadRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/SharePublicRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/AdminRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/FolderRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/ConfigMgr.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/StorageMgr.cpp
    ${CMAKE_SOURCE_DIR}/src/models/FileContents.cpp
    ${CMAKE_SOURCE_DIR}/src/models/Files.cpp
    ${CMAKE_SOURCE_DIR}/src/models/Folders.cpp
    ${CMAKE_SOURCE_DIR}/src/models/Trash.cpp
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
    ${CMAKE_SOURCE_DIR}/src/services/FileServiceUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/services/CleanupService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/ContentService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/QuotaService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/TrashService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/UploadLifecycleService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/TokenService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/RedisService.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/AdminAuthFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/ShareAuthFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/RegisterRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/DownloadRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/SharePublicRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/AdminRateLimitFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/FolderRateLimitFilter.cpp
    filters/AdminAuthFilter_test.cpp
    filters/RegisterRateLimit_test.cpp
    filters/DownloadRateLimit_test.cpp
    filters/SharePublicRateLimit_test.cpp
    filters/AdminRateLimit_test.cpp
    filters/FolderRateLimit_test.cpp
    PROPERTIES COMPILE_FLAGS "-include drogon/HttpAppFramework.h -include utils/LogHelper.hpp"
)

target_include_directories(disk-test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/test
)

target_link_libraries(disk-test
    PRIVATE
    Drogon::Drogon
    jwt-cpp::jwt-cpp
    unofficial-sodium::sodium
    spdlog::spdlog
    GTest::gtest
    GTest::gtest_main
)

gtest_discover_tests(disk-test)

add_test(
    NAME LoginRateLimitIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_login_rate_limit.py"
)

set_tests_properties(
    LoginRateLimitIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME UserProfileStorageIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_user_profile_storage.py"
)

set_tests_properties(
    UserProfileStorageIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME AssemblyBackpressureIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_assembly_backpressure.py"
)

set_tests_properties(
    AssemblyBackpressureIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME AuthFlowIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_auth_flow.py"
)

set_tests_properties(
    AuthFlowIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME AuthLifecycleIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_auth_lifecycle.py"
)

set_tests_properties(
    AuthLifecycleIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME CopyDeleteAtomicityIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_copy_delete_atomicity.py"
)

set_tests_properties(
    CopyDeleteAtomicityIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME DownloadFlowIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_download_flow.py"
)

set_tests_properties(
    DownloadFlowIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME DomainExtractionInvariantsIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_domain_extraction_invariants.py"
)

set_tests_properties(
    DomainExtractionInvariantsIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 180
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME FileMetadataQueryIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_file_metadata_query.py"
)

set_tests_properties(
    FileMetadataQueryIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME FileMutationOpsIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_file_mutation_ops.py"
)

set_tests_properties(
    FileMutationOpsIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME FolderLifecycleIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_folder_lifecycle.py"
)

set_tests_properties(
    FolderLifecycleIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME HealthAndLogsIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_health_and_logs.py"
)

set_tests_properties(
    HealthAndLogsIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME PasswordUpdateIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_password_update.py"
)

set_tests_properties(
    PasswordUpdateIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME RefreshTokenIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_refresh_token.py"
)

set_tests_properties(
    RefreshTokenIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME ShareBrowseIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_share_browse.py"
)

set_tests_properties(
    ShareBrowseIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME ShareManagementIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_share_management.py"
)

set_tests_properties(
    ShareManagementIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME SystemInfoIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_system_info.py"
)

set_tests_properties(
    SystemInfoIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME TrashLifecycleIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_trash_lifecycle.py"
)

set_tests_properties(
    TrashLifecycleIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME UploadFlowIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_upload_flow.py"
)

set_tests_properties(
    UploadFlowIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME UserProfileUpdateIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_user_profile_update.py"
)

set_tests_properties(
    UserProfileUpdateIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME FileDeleteRegressionIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_file_delete_regression.py"
)

set_tests_properties(
    FileDeleteRegressionIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME BrowseBurstRegressionIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_browse_burst_regression.py"
)

set_tests_properties(
    BrowseBurstRegressionIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME UploadRateLimitIntegration
    COMMAND uv run "${CMAKE_SOURCE_DIR}/test/integration/test_upload_rate_limit.py"
)

set_tests_properties(
    UploadRateLimitIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 300
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
