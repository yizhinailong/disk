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
    services/ShareService_test.cpp
    services/ShareServiceBatch_test.cpp
    services/ShareServiceQuery_test.cpp
    services/TokenService_test.cpp
    services/TokenServiceRevocation_test.cpp
    services/TrashService_test.cpp
    services/TrashServiceBatch_test.cpp
    services/CleanupService_test.cpp
    services/OperationLogJsonDetails_test.cpp
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
