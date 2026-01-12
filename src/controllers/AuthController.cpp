#include "AuthController.hpp"

namespace api {
    auto AuthController::Hello(const HttpRequestPtr& request, HttpResponseCallback&& callback) -> void {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setBody("Hello World!");
        resp->addHeader("Content-Type", "text/plain");
        std::move(callback)(resp);
    }
} // namespace api
