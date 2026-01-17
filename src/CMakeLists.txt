add_executable(${PROJECT_NAME} main.cpp)

# Controller
set(Controllers_HEADERS
    controllers/AuthController.hpp
)
set(Controllers_SOURCES
    controllers/AuthController.cpp
)

# Filter
set(Filters_HEADERS
    filters/JwtAuthFilter.hpp
)
set(Filters_SOURCES
    filters/JwtAuthFilter.cpp
)

# Model
set(Models_HEADERS
    models/FileContents.hpp
    models/Files.hpp
    models/Folders.hpp
    models/OperationLogs.hpp
    models/ShareFiles.hpp
    models/Shares.hpp
    models/Trash.hpp
    models/UploadTasks.hpp
    models/Users.hpp
)
set(Models_SOURCES
    models/FileContents.cpp
    models/Files.cpp
    models/Folders.cpp
    models/OperationLogs.cpp
    models/ShareFiles.cpp
    models/Shares.cpp
    models/Trash.cpp
    models/UploadTasks.cpp
    models/Users.cpp
)

# Requests
set(Requests_HEADERS
    requests/AuthRequest.hpp
)

# Service
set(Services_HEADERS
    services/AuthService.hpp
    services/TokenService.hpp
)
set(Services_SOURCES
    services/AuthService.cpp
    services/TokenService.cpp
)

# Utils
set(Utils_HEADERS
    utils/ErrorCode.hpp
    utils/PasswdHash.hpp
    utils/Pch.hpp
    utils/Response.hpp
)

target_sources(${PROJECT_NAME}
    PRIVATE
    ${Controllers_SOURCES}
    ${Filters_SOURCES}
    ${Models_SOURCES}
    ${Services_SOURCES}
    PUBLIC
    FILE_SET HEADERS
    TYPE HEADERS
    FILES
    ${Controllers_HEADERS}
    ${Filters_HEADERS}
    ${Models_HEADERS}
    ${Requests_HEADERS}
    ${Services_HEADERS}
    ${Utils_HEADERS}
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
