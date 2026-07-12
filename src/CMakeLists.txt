add_executable(${PROJECT_NAME} main.cpp)

# Application
set(Application_HEADERS
    application/ApplicationContext.hpp
)
set(Application_SOURCES
    application/ApplicationContext.cpp
)

# Controller
set(Controllers_HEADERS
    controllers/AdminController.hpp
    controllers/AuthController.hpp
    controllers/ControllerHelpers.hpp
    controllers/DownloadResponder.hpp
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
    controllers/AdminController.cpp
    controllers/AuthController.cpp
    controllers/DownloadResponder.cpp
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
    filters/AdminAuthFilter.hpp
    filters/AdminRateLimitFilter.hpp
    filters/JwtAuthFilter.hpp
    filters/RateLimitFilter.hpp
    filters/RateLimitHelper.hpp
    filters/RegisterRateLimitFilter.hpp
    filters/RequestTraceFilter.hpp
    filters/ShareAuthFilter.hpp
    filters/SharePublicRateLimitFilter.hpp
    filters/UploadRateLimitFilter.hpp
    filters/DownloadRateLimitFilter.hpp
    filters/FolderRateLimitFilter.hpp
)
set(Filters_SOURCES
    filters/AdminAuthFilter.cpp
    filters/AdminRateLimitFilter.cpp
    filters/JwtAuthFilter.cpp
    filters/RateLimitFilter.cpp
    filters/RegisterRateLimitFilter.cpp
    filters/RequestTraceFilter.cpp
    filters/ShareAuthFilter.cpp
    filters/SharePublicRateLimitFilter.cpp
    filters/UploadRateLimitFilter.cpp
    filters/DownloadRateLimitFilter.cpp
    filters/FolderRateLimitFilter.cpp
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
    models/UploadTaskChunks.hpp
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
    models/UploadTaskChunks.cpp
    models/UploadTasks.cpp
    models/Users.cpp
)

# Service
set(Services_HEADERS
    services/AdminService.hpp
    services/AuthService.hpp
    services/CleanupService.hpp
    services/ContentService.hpp
    services/FileListQuery.hpp
    services/FileMutationService.hpp
    services/FileQueryService.hpp
    services/FileRepository.hpp
    services/FileServiceUtils.hpp
    services/FolderRepository.hpp
    services/FolderService.hpp
    services/HealthService.hpp
    services/OperationLogService.hpp
    services/QuotaService.hpp
    services/RedisService.hpp
    services/ScheduledTasks.hpp
    services/SearchQuery.hpp
    services/ShareService.hpp
    services/SystemService.hpp
    services/TokenService.hpp
    services/TrashQuery.hpp
    services/TrashService.hpp
    services/TransactionRunner.hpp
    services/UploadLifecycleService.hpp
    services/UploadService.hpp
    services/UploadTaskRepository.hpp
    services/UserService.hpp
)
set(Services_SOURCES
    services/AdminService.cpp
    services/AuthService.cpp
    services/CleanupService.cpp
    services/ContentService.cpp
    services/FileListQuery.cpp
    services/FileMutationService.cpp
    services/FileQueryService.cpp
    services/FileRepository.cpp
    services/FileServiceUtils.cpp
    services/FolderRepository.cpp
    services/FolderService.cpp
    services/HealthService.cpp
    services/OperationLogService.cpp
    services/QuotaService.cpp
    services/RedisService.cpp
    services/ScheduledTasks.cpp
    services/SearchQuery.cpp
    services/ShareService.cpp
    services/SystemService.cpp
    services/TokenService.cpp
    services/TrashQuery.cpp
    services/TrashService.cpp
    services/UploadLifecycleService.cpp
    services/UploadService.cpp
    services/UploadTaskRepository.cpp
    services/UserService.cpp
)

# Storage
set(Storage_HEADERS
    storage/IFileStorage.hpp
    storage/LocalFileStorage.hpp
    storage/StorageMgr.hpp
)
set(Storage_SOURCES
    storage/LocalFileStorage.cpp
    storage/StorageMgr.cpp
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
    ${Application_SOURCES}
    ${Controllers_SOURCES}
    ${Filters_SOURCES}
    ${Models_SOURCES}
    ${Services_SOURCES}
    ${Storage_SOURCES}
    ${Utils_SOURCES}
    PUBLIC
    FILE_SET HEADERS
    TYPE HEADERS
    FILES
    ${Application_HEADERS}
    ${Controllers_HEADERS}
    ${Dtos_HEADERS}
    ${Filters_HEADERS}
    ${Models_HEADERS}
    ${Services_HEADERS}
    ${Storage_HEADERS}
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

# Drogon ORM model sources are generated and still use std::wstring_convert
# for UTF-8 length validation. Keep the warning contained to generated code.
set_source_files_properties(
    ${Models_SOURCES}
    PROPERTIES
    COMPILE_OPTIONS "-Wno-deprecated-declarations"
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    Drogon::Drogon
    jwt-cpp::jwt-cpp
    unofficial-sodium::sodium
    spdlog::spdlog
)
