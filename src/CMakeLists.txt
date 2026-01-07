add_executable(${PROJECT_NAME} main.cpp)

file(GLOB CTL_SRC
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/controllers/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/controllers/*.cc"
)

file(GLOB FILTER_SRC
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/filters/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/filters/*.cc"
)

file(GLOB PLUGIN_SRC
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/*.cc"
)

file(GLOB MODEL_SRC
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/models/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/models/*.cc"
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src
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

target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon)
