find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(disk-test
    main.cpp
    utils/PasswdHash_test.cpp
    requests/AuthRequest_test.cpp
)

target_include_directories(disk-test
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_precompile_headers(disk-test
    PRIVATE
    "${CMAKE_SOURCE_DIR}/src/utils/Pch.hpp"
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
