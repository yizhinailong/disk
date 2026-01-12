#pragma once

#include "utils/pch.hpp"

namespace api {
    class AuthController : public drogon::HttpController<AuthController> {
    public:
        METHOD_LIST_BEGIN
        ADD_METHOD_TO(AuthController::Hello, "/api/hello", drogon::Get);
        METHOD_LIST_END

        auto Hello(const HttpRequestPtr& request, HttpResponseCallback&& callback) -> void;
    };
} // namespace api
