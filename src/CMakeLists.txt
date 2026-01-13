add_executable(${PROJECT_NAME} main.cpp)

target_sources(${PROJECT_NAME}
    PRIVATE
    # Controllers
    controllers/AuthController.cpp
    controllers/AuthController.hpp

    # Models

    # Filters

    # Plugins

    # Utils
    utils/ErrorCode.hpp
    utils/Pch.hpp
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_precompile_headers(${PROJECT_NAME}
    PRIVATE
    "${CMAKE_SOURCE_DIR}/src/utils/Pch.hpp"
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    Drogon::Drogon
    jwt-cpp::jwt-cpp
)
