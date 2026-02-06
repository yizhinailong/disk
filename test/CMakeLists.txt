include(GoogleTest)

set(TEST_SOURCES
    main.cpp
    utils/PasswdHash_test.cpp
    utils/RedisKeyPrefix_test.cpp
    utils/Response_test.cpp
    services/HashUtil_test.cpp
    services/RedisService_test.cpp
    services/LoginRateLimit_test.cpp
    dtos/AuthDto_test.cpp
    dtos/UserDto_test.cpp
)

add_executable(disk-test ${TEST_SOURCES})

target_include_directories(disk-test PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(disk-test
    PRIVATE
    Drogon::Drogon
    jwt-cpp::jwt-cpp
    unofficial-sodium::sodium
    GTest::gtest
    GTest::gtest_main
)

gtest_discover_tests(disk-test)
