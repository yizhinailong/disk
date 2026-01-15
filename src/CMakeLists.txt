add_executable(${PROJECT_NAME} main.cpp)

target_sources(${PROJECT_NAME}
    PRIVATE
    # Controller
    controllers/AuthController.cpp
    # Model
    models/FileContents.cpp
    models/Files.cpp
    models/Folders.cpp
    models/OperationLogs.cpp
    models/ShareFiles.cpp
    models/Shares.cpp
    models/Trash.cpp
    models/UploadTasks.cpp
    models/Users.cpp
    # Service
    services/AuthService.cpp
    PUBLIC
    FILE_SET HEADERS
    TYPE HEADERS
    FILES
    # Controllers
    controllers/AuthController.hpp
    # Models
    models/FileContents.hpp
    models/Files.hpp
    models/Folders.hpp
    models/OperationLogs.hpp
    models/ShareFiles.hpp
    models/Shares.hpp
    models/Trash.hpp
    models/UploadTasks.hpp
    models/Users.hpp
    # Requests
    requests/AuthRequest.hpp
    # Services
    services/AuthService.hpp
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
