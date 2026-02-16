#include <drogon/drogon.h>

#include "services/CleanupService.hpp"

auto main() -> int {
    LOG_INFO << "网盘系统启动中...";

    // 初始化 libsodium 加密库
    if (sodium_init() < 0) {
        LOG_ERROR << "libsodium 初始化失败";
        return 1;
    }
    LOG_INFO << "libsodium 初始化成功";

    LOG_INFO << "Drogon 框架版本：" << drogon::getVersion();
    LOG_INFO << "Web 服务监听在 http://127.0.0.1:8080";

    // 注册启动后定时清理任务
    drogon::app().registerBeginningAdvice([]() {
        auto cleanup_service = std::make_shared<disk::services::CleanupService>(
            drogon::app().getDbClient()
        );

        // async_func wraps the coroutine lambda in std::function<void()> that keeps
        // captures alive. runEvery stores the function for program lifetime.
        drogon::app().getLoop()->runEvery(
            3600.0,
            drogon::async_func([cleanup_service]() -> drogon::Task<void> { // NOLINT(cppcoreguidelines-avoid-capturing-lambda-coroutines)
                LOG_INFO << "定时清理任务开始执行";
                const auto& service = cleanup_service;
                auto result = co_await service->CleanupExpiredTrash();
                if (!result) {
                    LOG_ERROR << "定时清理任务失败: " << result.error().message;
                }
            })
        );

        LOG_INFO << "定时清理任务已注册（每小时执行）";
    });

    drogon::app().loadConfigFile("config.json").run();

    return 0;
}
