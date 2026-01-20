find_package(GTest REQUIRED)
include(GoogleTest)

# ==================== 源文件定义 ====================
set(Controllers_SOURCES ../src/controllers/AuthController.cpp)
set(Filters_SOURCES ../src/filters/JwtAuthFilter.cpp)

set(Models_SOURCES
    ../src/models/FileContents.cpp
    ../src/models/Files.cpp
    ../src/models/Folders.cpp
    ../src/models/OperationLogs.cpp
    ../src/models/ShareFiles.cpp
    ../src/models/Shares.cpp
    ../src/models/Trash.cpp
    ../src/models/UploadTasks.cpp
    ../src/models/Users.cpp
)

set(Services_SOURCES
    ../src/services/AuthService.cpp
    ../src/services/TokenService.cpp
)

set(Utils_SOURCES ../src/utils/ConfigMgr.cpp)

# ==================== 测试可执行文件 ====================
add_executable(disk-test
    main.cpp
    utils/PasswdHash_test.cpp
    requests/AuthRequest_test.cpp

    # 直接编译所有项目源文件
    ${Controllers_SOURCES}
    ${Filters_SOURCES}
    ${Models_SOURCES}
    ${Services_SOURCES}
    ${Utils_SOURCES}
)

target_include_directories(disk-test
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)

target_precompile_headers(disk-test
    PRIVATE "${CMAKE_SOURCE_DIR}/src/utils/Pch.hpp"
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
