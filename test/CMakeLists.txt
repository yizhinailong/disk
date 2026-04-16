enable_testing()

include(GoogleTest)

set(TEST_SOURCES
    main.cpp
    utils/ConfigMgr_test.cpp
    utils/FileHashUtil_test.cpp
    utils/PasswdHash_test.cpp
    utils/RedisKeyPrefix_test.cpp
    utils/Response_test.cpp
    services/HashUtil_test.cpp
    services/RedisService_test.cpp
    services/LoginRateLimit_test.cpp
    services/FileUploadConsistency_test.cpp
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
    services/UserService_test.cpp
    services/CleanupService_test.cpp
    services/SystemService_test.cpp
    services/OperationLogJsonDetails_test.cpp
    services/OperationLogService_test.cpp
    filters/ShareAuthFilter_test.cpp
    filters/JwtAuthFilter_test.cpp
    dtos/AuthDto_test.cpp
    dtos/FileDto_test.cpp
    dtos/FolderDto_test.cpp
    dtos/ShareDto_test.cpp
    dtos/TrashDto_test.cpp
    dtos/UserDto_test.cpp
    storage/AssemblyWorkerPool_test.cpp
    ${CMAKE_SOURCE_DIR}/src/services/CleanupService.cpp
    ${CMAKE_SOURCE_DIR}/src/filters/ShareAuthFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/ConfigMgr.cpp
    ${CMAKE_SOURCE_DIR}/src/storage/StorageMgr.cpp
    ${CMAKE_SOURCE_DIR}/src/models/FileContents.cpp
    ${CMAKE_SOURCE_DIR}/src/services/TokenService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/RedisService.cpp
)

add_executable(disk-test ${TEST_SOURCES})

# ConfigMgr.cpp uses LOG_* macros from trantor but doesn't include the header directly.
# In the main build this works via PCH; for the test target we force-include it.
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/src/utils/ConfigMgr.cpp
    PROPERTIES COMPILE_FLAGS "-include drogon/HttpAppFramework.h"
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
    GTest::gtest
    GTest::gtest_main
)

gtest_discover_tests(disk-test)

add_test(
    NAME LoginRateLimitIntegration
    COMMAND bash "${CMAKE_SOURCE_DIR}/test/integration/test_login_rate_limit.sh"
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
    COMMAND bash "${CMAKE_SOURCE_DIR}/test/integration/test_user_profile_storage.sh"
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
    COMMAND bash "${CMAKE_SOURCE_DIR}/test/integration/test_assembly_backpressure.sh"
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
    COMMAND bash "${CMAKE_SOURCE_DIR}/test/integration/test_auth_flow.sh"
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
    COMMAND bash "${CMAKE_SOURCE_DIR}/test/integration/test_auth_lifecycle.sh"
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
    COMMAND bash "${CMAKE_SOURCE_DIR}/test/integration/test_copy_delete_atomicity.sh"
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
    COMMAND bash "${CMAKE_SOURCE_DIR}/test/integration/test_download_flow.sh"
)

set_tests_properties(
    DownloadFlowIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME FileMetadataQueryIntegration
    COMMAND bash "${CMAKE_SOURCE_DIR}/test/integration/test_file_metadata_query.sh"
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
    COMMAND bash "${CMAKE_SOURCE_DIR}/test/integration/test_file_mutation_ops.sh"
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
    COMMAND bash "${CMAKE_SOURCE_DIR}/test/integration/test_folder_lifecycle.sh"
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
    COMMAND bash "${CMAKE_SOURCE_DIR}/test/integration/test_health_and_logs.sh"
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
    COMMAND bash "${CMAKE_SOURCE_DIR}/test/integration/test_password_update.sh"
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
    COMMAND bash "${CMAKE_SOURCE_DIR}/test/integration/test_refresh_token.sh"
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
    COMMAND bash "${CMAKE_SOURCE_DIR}/test/integration/test_share_browse.sh"
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
    COMMAND bash "${CMAKE_SOURCE_DIR}/test/integration/test_share_management.sh"
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
    COMMAND bash "${CMAKE_SOURCE_DIR}/test/integration/test_system_info.sh"
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
    COMMAND bash "${CMAKE_SOURCE_DIR}/test/integration/test_trash_lifecycle.sh"
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
    COMMAND bash "${CMAKE_SOURCE_DIR}/test/integration/test_upload_flow.sh"
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
    COMMAND bash "${CMAKE_SOURCE_DIR}/test/integration/test_user_profile_update.sh"
)

set_tests_properties(
    UserProfileUpdateIntegration
    PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
