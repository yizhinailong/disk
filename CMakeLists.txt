cmake_minimum_required(VERSION 4.0)
project(disk CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(${PROJECT_NAME} src/main.cpp)

find_package(Drogon CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon)

file(GLOB CTL_SRC
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/controllers/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/controllers/*.cc"
)

file(GLOB FILTER_SRC
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/filters/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/filters/*.cc"
)

file(GLOB PLUGIN_SRC
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/*.cc"
)

file(GLOB MODEL_SRC
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/models/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/models/*.cc"
)

target_include_directories(${PROJECT_NAME}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    ${MODEL_SRC}
)

target_sources(${PROJECT_NAME}
    PRIVATE
    ${SRC_DIR}
    ${CTL_SRC}
    ${FILTER_SRC}
    ${PLUGIN_SRC}
    ${MODEL_SRC}
)

target_precompile_headers(${PROJECT_NAME}
    PRIVATE
    <drogon/drogon.h>
    <drogon/HttpAppFramework.h>
    <drogon/HttpController.h>
)

add_subdirectory(test)
