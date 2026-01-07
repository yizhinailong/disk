cmake_minimum_required(VERSION 4.0)
project(disk CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Drogon CONFIG REQUIRED)

include(${CMAKE_SOURCE_DIR}/cmake/setup_pch.cmake)

add_subdirectory(src)
add_subdirectory(test)
