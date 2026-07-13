#include <memory>

#include <drogon/drogon.h>

#include "application/ApplicationContext.hpp"
#include "services/CleanupService.hpp"
#include "services/ScheduledTasks.hpp"
#include "services/TokenService.hpp"
#include "storage/BlobStoreMgr.hpp"
#include "storage/StorageFactory.hpp"
#include "storage/StorageMgr.hpp"
#include "utils/ConfigMgr.hpp"
#include "utils/LogHelper.hpp"

auto main() -> int {
    disk::utils::Logger::Init();
    disk::utils::Logger::CaptureFrameworkLogs();

    disk::utils::Logger::Info() << "Disk system starting...";

    /// 初始化 libsodium 加密库
    if (sodium_init() < 0) {
        disk::utils::Logger::Error() << "libsodium initialization failed";
        return 1;
    }
    disk::utils::Logger::Info() << "libsodium initialized successfully";

    /// 加载配置文件
    drogon::app().loadConfigFile("config.json");
    disk::utils::Logger::Info() << "Configuration file loaded successfully";

    /// 使用 config.json 中的值初始化 ConfigMgr
    disk::utils::ConfigMgr::GetInstance()->LoadConfig();

    /// 验证配置（JWT_SECRET 所有环境必须设置，DATABASE/REDIS 仅安全模式要求）
    try {
        disk::utils::ConfigMgr::GetInstance()->ValidateSecureConfig();
    } catch (const std::runtime_error& e) {
        disk::utils::Logger::Error() << "Secure config validation failed: " << e.what();
        return 1;
    }

    /// 初始化 TokenService 单例（启动时一次性完成）
    disk::services::TokenService::Initialize(
        disk::utils::ConfigMgr::GetInstance()->GetJwtSecret()
    );
    disk::utils::Logger::Info() << "TokenService initialized successfully";

    /// 记录有效存储路径
    disk::utils::Logger::Info() << "Effective storage configuration:";
    disk::utils::Logger::Info() << "  storage_base_path: "
                << disk::utils::ConfigMgr::GetInstance()->GetStorageBasePath();
    disk::utils::Logger::Info() << "  temp_upload_path: " << disk::utils::ConfigMgr::GetInstance()->GetTempUploadPath();
    disk::utils::Logger::Info() << "  chunk_size: " << disk::utils::ConfigMgr::GetInstance()->GetChunkSize();
    disk::utils::Logger::Info() << "  max_file_size: " << disk::utils::ConfigMgr::GetInstance()->GetMaxFileSize();
    disk::utils::Logger::Info() << "  upload_task_expiry_seconds: "
                << disk::utils::ConfigMgr::GetInstance()->GetUploadTaskExpirySeconds();
    disk::utils::Logger::Info() << "  assembly_max_concurrent: "
                << disk::utils::ConfigMgr::GetInstance()->GetAssemblyMaxConcurrent();
    disk::utils::Logger::Info() << "  assemble_buffer_size_bytes: "
                << disk::utils::ConfigMgr::GetInstance()->GetAssembleBufferSizeBytes();

    /// 初始化文件存储和最终 Blob 存储
    try {
        auto storage_bundle = disk::storage::StorageFactory::Create(disk::utils::ConfigMgr::GetInstance());
        disk::storage::StorageMgr::SetInstance(std::move(storage_bundle.storage));
        disk::storage::BlobStoreMgr::SetInstance(std::move(storage_bundle.blob_store));
    } catch (const std::runtime_error& e) {
        disk::utils::Logger::Error() << "File storage initialization failed: " << e.what();
        return 1;
    }
    disk::utils::Logger::Info() << "File storage initialized successfully";

    disk::utils::Logger::Info() << "Drogon framework version: " << drogon::getVersion();
    disk::utils::Logger::Info() << "Web server listening on http://127.0.0.1:8080";

    /// 注册启动后服务组合与定时清理任务
    drogon::app().registerBeginningAdvice([]() {
        disk::application::ApplicationContext::Initialize(
            drogon::app().getDbClient(),
            drogon::app().getRedisClient(),
            disk::storage::StorageMgr::GetStorage(),
            disk::storage::BlobStoreMgr::GetBlobStore(),
            disk::utils::ConfigMgr::GetInstance()->GetJwtSecret()
        );
        disk::utils::Logger::Info() << "Application service context initialized successfully";

        disk::services::ScheduledTasks::Initialize(
            std::shared_ptr<disk::services::CleanupService>(
                disk::application::ApplicationContext::GetInstance(),
                &disk::application::ApplicationContext::GetInstance()->Cleanup()
            )
        );
        disk::services::ScheduledTasks::Register();
        disk::services::TokenService::GetInstance()->StartCacheMaintenance();
    });

    /// 为所有响应添加 X-Request-Id 头（从 RequestTraceFilter 设置的 attributes 中读取）
    drogon::app().registerPostHandlingAdvice(
        [](const drogon::HttpRequestPtr& req, const drogon::HttpResponsePtr& resp) {
            auto attrs = req->attributes();
            if (attrs->find("request_id")) {
                resp->addHeader("X-Request-Id", attrs->get<std::string>("request_id"));
            }
        }
    );

    drogon::app().run();

    return 0;
}
