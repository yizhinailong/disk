add_executable(${PROJECT_NAME} main.cpp)

target_sources(${PROJECT_NAME}
    PRIVATE
    # Controllers
    controllers/AuthController.cpp
    controllers/AuthController.hpp
    # Models
    models/FileContents.cpp
    models/FileContents.hpp
    models/Files.cpp
    models/Files.hpp
    models/Users.cpp
    models/Users.hpp
    # Requests
    requests/AuthRequest.hpp
    # Filters

    # Plugins

    # Services
    services/AuthService.cpp
    services/AuthService.hpp
    # Requests
    requests/AuthRequest.hpp
    # Utils
    utils/ErrorCode.hpp
    utils/PasswdHash.hpp
    utils/Pch.hpp
    utils/Response.hpp
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
    unofficial-sodium::sodium
)
