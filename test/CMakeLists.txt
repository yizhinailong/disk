enable_testing()

include(GoogleTest)

set(TEST_SOURCES
    main.cpp
    utils/FileHashUtil_test.cpp
    utils/PasswdHash_test.cpp
    utils/RedisKeyPrefix_test.cpp
    utils/Response_test.cpp
    services/HashUtil_test.cpp
    services/RedisService_test.cpp
    services/LoginRateLimit_test.cpp
    services/ShareService_test.cpp
    services/TokenService_test.cpp
    services/TrashService_test.cpp
    services/OperationLogJsonDetails_test.cpp
    filters/ShareAuthFilter_test.cpp
    dtos/AuthDto_test.cpp
    dtos/FileDto_test.cpp
    dtos/FolderDto_test.cpp
    dtos/ShareDto_test.cpp
    dtos/TrashDto_test.cpp
    dtos/UserDto_test.cpp
    ${CMAKE_SOURCE_DIR}/src/services/TokenService.cpp
    ${CMAKE_SOURCE_DIR}/src/services/RedisService.cpp
)

add_executable(disk-test ${TEST_SOURCES})

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
