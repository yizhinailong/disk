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

    drogon::app().loadConfigFile("config.json").run();

    return 0;
}
