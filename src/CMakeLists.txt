add_executable(${PROJECT_NAME} main.cpp)

# Controller
set(Controllers_HEADERS
    controllers/AuthController.hpp
    controllers/FileController.hpp
    controllers/FolderController.hpp
    controllers/HealthController.hpp
    controllers/OperationLogController.hpp
    controllers/ShareController.hpp
    controllers/SystemController.hpp
    controllers/TrashController.hpp
    controllers/UserController.hpp
)
set(Controllers_SOURCES
    controllers/AuthController.cpp
    controllers/FileController.cpp
    controllers/FolderController.cpp
    controllers/HealthController.cpp
    controllers/OperationLogController.cpp
    controllers/ShareController.cpp
    controllers/SystemController.cpp
    controllers/TrashController.cpp
    controllers/UserController.cpp
)

# Dtos
set(Dtos_HEADERS
    dtos/AuthDto.hpp
    dtos/FileDto.hpp
    dtos/FolderDto.hpp
    dtos/ShareDto.hpp
    dtos/TrashDto.hpp
    dtos/UserDto.hpp
)

# Filter
set(Filters_HEADERS
    filters/JwtAuthFilter.hpp
    filters/RateLimitFilter.hpp
    filters/ShareAuthFilter.hpp
    filters/UploadRateLimitFilter.hpp
)
set(Filters_SOURCES
    filters/JwtAuthFilter.cpp
    filters/RateLimitFilter.cpp
    filters/ShareAuthFilter.cpp
    filters/UploadRateLimitFilter.cpp
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

# Service
set(Services_HEADERS
    services/AuthService.hpp
    services/CleanupService.hpp
    services/FileService.hpp
    services/FolderService.hpp
    services/HealthService.hpp
    services/OperationLogService.hpp
    services/RedisService.hpp
    services/ShareService.hpp
    services/SystemService.hpp
    services/TokenService.hpp
    services/TrashService.hpp
    services/UserService.hpp
)
set(Services_SOURCES
    services/AuthService.cpp
    services/CleanupService.cpp
    services/FileService.cpp
    services/FolderService.cpp
    services/HealthService.cpp
    services/OperationLogService.cpp
    services/RedisService.cpp
    services/ShareService.cpp
    services/SystemService.cpp
    services/TokenService.cpp
    services/TrashService.cpp
    services/UserService.cpp
)

# Utils
set(Utils_HEADERS
    utils/ConfigMgr.hpp
    utils/ErrorCode.hpp
    utils/FileHashUtil.hpp
    utils/HashUtil.hpp
    utils/Pch.hpp
    utils/RedisKeyPrefix.hpp
    utils/Response.hpp
    utils/Singleton.hpp
)
set(Utils_SOURCES
    utils/ConfigMgr.cpp
)

target_sources(${PROJECT_NAME}
    PRIVATE
    ${Controllers_SOURCES}
    ${Filters_SOURCES}
    ${Models_SOURCES}
    ${Services_SOURCES}
    ${Utils_SOURCES}
    PUBLIC
    FILE_SET HEADERS
    TYPE HEADERS
    FILES
    ${Controllers_HEADERS}
    ${Dtos_HEADERS}
    ${Filters_HEADERS}
    ${Models_HEADERS}
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
