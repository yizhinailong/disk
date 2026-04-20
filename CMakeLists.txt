cmake_minimum_required(VERSION 4.0)
project(disk CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Drogon CONFIG REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

enable_testing()

# Desktop client (optional, OFF by default)
option(DISK_ENABLE_DESKTOP "Build the Qt 6 desktop client" OFF)
if(DISK_ENABLE_DESKTOP)
    add_subdirectory(clients/desktop)
endif()

add_subdirectory(src)
add_subdirectory(test)
