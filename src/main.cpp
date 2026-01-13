auto main() -> int {
    std::println("Drogon version: {}", drogon::getVersion());
    std::println("Web server is listening on http://127.0.0.1:8080");

    drogon::app().loadConfigFile("config.json").run();

    return 0;
}
