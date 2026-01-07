#include "utils/pch.hpp"

int main() {
    drogon::app().registerHandler(
        "/hello",
        [](const drogon::HttpRequestPtr& req, HttpResponseCallback&& callback) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setBody("Hello World");
            std::move(callback)(resp);
        });

    std::println("Drogon version: {}", drogon::getVersion());
    std::println("Web server is listening on http://127.0.0.1:8080");
    std::println("The index page is http://127.0.0.1:8080/hello");

    drogon::app().loadConfigFile("config.json").run();

    return 0;
}
