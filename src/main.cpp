#include <drogon/drogon.h>

#include "services/ScheduledTasks.hpp"

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
        disk::services::ScheduledTasks::Initialize(drogon::app().getDbClient());
        disk::services::ScheduledTasks::Register();
    });

    drogon::app().loadConfigFile("config.json").run();

    return 0;
}
