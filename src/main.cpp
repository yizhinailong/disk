#include <drogon/drogon.h>

#include "services/CleanupService.hpp"

auto main() -> int {
    LOG_INFO << "Disk system starting...";

    // 初始化 libsodium 加密库
    if (sodium_init() < 0) {
        LOG_ERROR << "libsodium initialization failed";
        return 1;
    }
    LOG_INFO << "libsodium initialized successfully";

    LOG_INFO << "Drogon framework version: " << drogon::getVersion();
    LOG_INFO << "Web server listening on http://127.0.0.1:8080";

    // 注册启动后定时清理任务
    drogon::app().registerBeginningAdvice([]() {
        using disk::services::CleanupService;
        auto cleanup_service = std::make_shared<CleanupService>(drogon::app().getDbClient());

        // async_func wraps the coroutine lambda in std::function<void()> that keeps
        // captures alive. runEvery stores the function for program lifetime.
        drogon::app().getLoop()->runEvery(
            3600.0,
            drogon::async_func([cleanup_service]() -> drogon::Task<void> {
                LOG_INFO << "Scheduled cleanup task started";
                const auto& service = cleanup_service;
                auto result = co_await service->CleanupExpiredTrash();
                if (!result) {
                    LOG_ERROR << "Scheduled cleanup task failed: " << result.error().message;
                }
            })
        );

        LOG_INFO << "Scheduled cleanup task registered (runs every hour)";
    });

    drogon::app().loadConfigFile("config.json").run();

    return 0;
}
