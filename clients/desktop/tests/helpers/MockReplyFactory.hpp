/**
 * @file MockReplyFactory.hpp
 * @brief Factory helpers for creating common mock network replies
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>

#include "MockNetworkAccessManager.hpp"

namespace disk::desktop::testing {

    /**
     * @brief Factory for common mock reply patterns used across test suites
     */
    class MockReplyFactory {
    public:
        /**
         * @brief Create a successful API response with JSON data
         */
        static auto MakeJsonResponse(
            const QJsonObject& data,
            int http_status = 200
        ) -> MockNetworkReply* {
            auto body = QJsonDocument(data).toJson(QJsonDocument::Compact);
            return new MockNetworkReply(body, http_status);
        }

        /**
         * @brief Create a backend error response
         */
        static auto MakeErrorResponse(
            int code,
            const QString& message,
            int http_status = 400
        ) -> MockNetworkReply* {
            QJsonObject error_json;
            error_json["code"] = code;
            error_json["message"] = message;
            error_json["data"] = QJsonValue::Null;

            auto body = QJsonDocument(error_json).toJson(QJsonDocument::Compact);
            return new MockNetworkReply(body, http_status);
        }

        /**
         * @brief Create a network-level error (no HTTP response received)
         */
        static auto MakeNetworkError(
            QNetworkReply::NetworkError error,
            const QString& error_string
        ) -> MockNetworkReply* {
            return new MockNetworkReply(error, error_string);
        }

        /**
         * @brief Create a successful login response
         */
        static auto LoginSuccessResponse() -> QJsonObject {
            return {
                {    "code",0                            },
                { "message", "success" },
                {    "data",
                 QJsonObject{
                 { "access_token", "eyJhbGciOiJIUzI1NiJ9.eyJ1c2VyX2lkIjoxfQ.test" },
                 { "refresh_token", "rt_test_refresh_001" },
                 { "token_type", "Bearer" },
                 { "expires_in", 7200 },
                 { "user",
                 QJsonObject{
                 { "id", 1 },
                 { "username", "testuser" },
                 { "email", "test@example.com" },
                 { "nickname", "Test User" },
                 { "storage_used", 1048576 },
                 { "storage_quota", qint64{ 107374182400LL } },
                 } } }                }
            };
        }

        /**
         * @brief Create a successful register response
         */
        static auto RegisterSuccessResponse() -> QJsonObject {
            return {
                {    "code",0                            },
                { "message", "success" },
                {    "data",
                 QJsonObject{
                 { "user",
                 QJsonObject{
                 { "id", 2 },
                 { "username", "newuser" },
                 { "email", "newuser@example.com" },
                 { "nickname", "" },
                 { "storage_used", 0 },
                 { "storage_quota", qint64{ 107374182400LL } },
                 } } }                }
            };
        }

        /**
         * @brief Create a token refresh success response
         */
        static auto RefreshSuccessResponse() -> QJsonObject {
            return {
                {    "code",0                            },
                { "message", "success" },
                {    "data",
                 QJsonObject{
                 { "access_token", "eyJhbGciOiJIUzI1NiJ9.eyJ1c2VyX2lkIjoxfQ.refreshed" },
                 { "refresh_token", "rt_test_refresh_002" },
                 { "token_type", "Bearer" },
                 { "expires_in", 7200 },
                 }                    }
            };
        }

        /**
         * @brief Create a share access success response
         */
        static auto ShareAccessSuccessResponse() -> QJsonObject {
            return {
                {    "code",0                            },
                { "message", "success" },
                {    "data",
                 QJsonObject{
                 { "share_token", "st_test_visitor_001" },
                 { "expires_in", 3600 },
                 { "permission", "download" },
                 { "files",
                 QJsonArray{
                 QJsonObject{
                 { "id", 101 },
                 { "type", "file" },
                 { "name", "readme.txt" },
                 { "size", 2048 },
                 },
                 } } }                }
            };
        }
    };

} // namespace disk::desktop::testing
