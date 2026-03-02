#pragma once

#include "ApiReply.hpp"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>
#include <functional>

namespace disk::qml::api {

    /// Callback type for PostJson: receives a structured ApiReply.
    /// On success: reply.error.httpStatusSuccess is true, reply.json may have value.
    /// On transport failure: reply.error.hasNetworkError is true.
    using PostJsonCallback = std::function<void(ApiReply reply)>;

    /// Minimal network abstraction for mocking in tests.
    class IApiClient {
    public:
        virtual ~IApiClient() = default;

        virtual auto SetBaseUrl(const QUrl& url) -> void = 0;
        virtual auto SetBearerToken(const QString& token) -> void = 0;

        /// Issue an async POST with JSON body.
        /// @param path    API path (e.g. "/api/auth/login")
        /// @param body    JSON request body
        /// @param ctx     Context object – callback is NOT invoked if ctx is destroyed
        /// @param cb      Completion callback (always on main thread)
        virtual auto PostJson(const QString& path, const QJsonObject& body, QObject* ctx, PostJsonCallback cb) -> void = 0;
    };

} // namespace disk::qml::api
