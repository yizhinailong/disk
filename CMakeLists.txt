cmake_minimum_required(VERSION 4.0)
project(disk CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Drogon CONFIG REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)

# 使用 pkg-config 查找 libxcrypt
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBCRYPT REQUIRED IMPORTED_TARGET libcrypt)

add_subdirectory(src)
add_subdirectory(test)
