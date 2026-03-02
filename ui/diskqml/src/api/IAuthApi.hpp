#pragma once

#include <QObject>
#include <QString>
#include <functional>

#include <models/AuthDtos.hpp>

namespace disk::qml::api {

    /// Callback type for auth API calls: receives an ApiEnvelope on success,
    /// or a network-level error string on transport failure.
    /// When networkError is non-empty, envelope is default-constructed (meaningless).
    using AuthApiCallback =
        std::function<void(models::ApiEnvelope envelope, QString networkError)>;

    /// Pure virtual auth API interface for mocking in tests.
    class IAuthApi {
    public:
        virtual ~IAuthApi() = default;

        virtual auto Register(const QString& username, const QString& email, const QString& password, QObject* ctx, AuthApiCallback cb) -> void = 0;

        virtual auto Login(const QString& account, const QString& password, QObject* ctx, AuthApiCallback cb) -> void = 0;

        virtual auto Refresh(const QString& refreshToken, QObject* ctx, AuthApiCallback cb) -> void = 0;

        virtual auto Logout(const QString& accessToken, QObject* ctx, AuthApiCallback cb) -> void = 0;
    };

} // namespace disk::qml::api
