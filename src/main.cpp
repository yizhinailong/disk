auto main() -> int {
    // 初始化 libsodium 加密库
    if (sodium_init() < 0) {
        std::println(stderr, "错误：libsodium 初始化失败");
        return 1;
    }

    std::println("Drogon version: {}", drogon::getVersion());
    std::println("Web server is listening on http://127.0.0.1:8080");

    drogon::app().loadConfigFile("config.json").run();

    return 0;
}
