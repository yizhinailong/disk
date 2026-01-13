#pragma once

namespace disk {
    class AuthController : public drogon::HttpController<AuthController> {
    public:
        METHOD_LIST_BEGIN
        ADD_METHOD_TO(AuthController::Register, "/api/auth/register", drogon::Post);
        METHOD_LIST_END

        auto Register(const HttpRequestPtr& request, HttpResponseCallback&& callback) -> void;
    };
} // namespace disk
