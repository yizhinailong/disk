#include <drogon/drogon.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <trantor/utils/Logger.h>

#include "services/ScheduledTasks.hpp"
#include "services/TokenService.hpp"
#include "storage/LocalFileStorage.hpp"
#include "storage/StorageMgr.hpp"
#include "utils/ConfigMgr.hpp"

auto main() -> int {
    // 初始化 spdlog：控制台 + 文件轮转
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        "build/log/disk.log", 10485760, 10);
    auto logger = std::make_shared<spdlog::logger>(
        "disk", spdlog::sinks_init_list{console_sink, file_sink});
    logger->set_pattern("%v");
    logger->flush_on(spdlog::level::err);
    spdlog::set_default_logger(logger);

    // 将 Trantor 日志输出重定向到 spdlog（Trantor 已格式化消息，spdlog 仅负责分发到各 sink）
    trantor::Logger::setOutputFunction(
        [](const char* msg, uint64_t len) {
            spdlog::default_logger_raw()->info(std::string_view(msg, len));
        },
        []() {
            spdlog::default_logger_raw()->flush();
        }
    );

    LOG_INFO << "Disk system starting...";

    // 初始化 libsodium 加密库
    if (sodium_init() < 0) {
        LOG_ERROR << "libsodium initialization failed";
        return 1;
    }
    LOG_INFO << "libsodium initialized successfully";

    // 加载配置文件
    drogon::app().loadConfigFile("config.json");
    LOG_INFO << "Configuration file loaded successfully";

    // 使用 config.json 中的值初始化 ConfigMgr
    disk::utils::ConfigMgr::GetInstance()->LoadConfig();

    // 验证配置（JWT_SECRET 所有环境必须设置，DATABASE/REDIS 仅安全模式要求）
    try {
        disk::utils::ConfigMgr::GetInstance()->ValidateSecureConfig();
    } catch (const std::runtime_error& e) {
        LOG_ERROR << "Secure config validation failed: " << e.what();
        return 1;
    }

    // 初始化 TokenService 单例（启动时一次性完成）
    disk::services::TokenService::Initialize(
        disk::utils::ConfigMgr::GetInstance()->GetJwtSecret()
    );
    LOG_INFO << "TokenService initialized successfully";

    // 记录有效存储路径
    LOG_INFO << "Effective storage configuration:";
    LOG_INFO << "  storage_base_path: "
             << disk::utils::ConfigMgr::GetInstance()->GetStorageBasePath();
    LOG_INFO << "  temp_upload_path: " << disk::utils::ConfigMgr::GetInstance()->GetTempUploadPath();
    LOG_INFO << "  chunk_size: " << disk::utils::ConfigMgr::GetInstance()->GetChunkSize();
    LOG_INFO << "  max_file_size: " << disk::utils::ConfigMgr::GetInstance()->GetMaxFileSize();
    LOG_INFO << "  upload_task_expiry_seconds: "
             << disk::utils::ConfigMgr::GetInstance()->GetUploadTaskExpirySeconds();
    LOG_INFO << "  assembly_max_concurrent: "
             << disk::utils::ConfigMgr::GetInstance()->GetAssemblyMaxConcurrent();
    LOG_INFO << "  assemble_buffer_size_bytes: "
             << disk::utils::ConfigMgr::GetInstance()->GetAssembleBufferSizeBytes();

    // 初始化文件存储
    auto storage = std::make_shared<disk::storage::LocalFileStorage>(disk::utils::ConfigMgr::GetInstance());
    disk::storage::StorageMgr::SetInstance(storage);
    LOG_INFO << "File storage initialized successfully";

    LOG_INFO << "Drogon framework version: " << drogon::getVersion();
    LOG_INFO << "Web server listening on http://127.0.0.1:8080";

    // 注册启动后定时清理任务
    drogon::app().registerBeginningAdvice([]() {
        disk::services::ScheduledTasks::Initialize(drogon::app().getDbClient());
        disk::services::ScheduledTasks::Register();
        disk::services::TokenService::GetInstance()->StartCacheMaintenance();
    });

    // 为所有响应添加 X-Request-Id 头（从 RequestTraceFilter 设置的 attributes 中读取）
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
