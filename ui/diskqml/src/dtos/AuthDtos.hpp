/**
 * @file AuthDtos.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Auth request/response DTOs and JSON parsing helpers for the QML client
 * @version 0.1
 * @date 2026-03-02
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <optional>

namespace disk::qml::models {

    // ==================== Request DTOs ====================

    /**
     * @brief Registration request DTO
     *
     * @details
     * JSON keys produced by ToJsonObject():
     * - "username": display name for the new account
     * - "email":    account e-mail address
     * - "password": plain-text password (transmitted over TLS)
     */
    struct RegisterRequest {
        QString username;
        QString email;
        QString password;

        [[nodiscard]] auto ToJsonObject() const -> QJsonObject {
            QJsonObject obj;
            obj.insert(QLatin1String("username"), username);
            obj.insert(QLatin1String("email"), email);
            obj.insert(QLatin1String("password"), password);
            return obj;
        }
    };

    /**
     * @brief Login request DTO
     *
     * @details
     * JSON keys produced by ToJsonObject():
     * - "account":  username or e-mail accepted by the server
     * - "password": plain-text password (transmitted over TLS)
     */
    struct LoginRequest {
        QString account;
        QString password;

        [[nodiscard]] auto ToJsonObject() const -> QJsonObject {
            QJsonObject obj;
            obj.insert(QLatin1String("account"), account);
            obj.insert(QLatin1String("password"), password);
            return obj;
        }
    };

    /**
     * @brief Token-refresh request DTO
     *
     * @details
     * JSON keys produced by ToJsonObject():
     * - "refresh_token": single-use refresh token obtained at login
     */
    struct RefreshTokenRequest {
        QString refreshToken;

        [[nodiscard]] auto ToJsonObject() const -> QJsonObject {
            QJsonObject obj;
            obj.insert(QLatin1String("refresh_token"), refreshToken);
            return obj;
        }
    };

    // ==================== Response DTOs ====================
    // ---------------------------------------------------------------------------
    // Plain data structs (no Q_OBJECT / Q_PROPERTY)
    // ---------------------------------------------------------------------------

    /**
     * @brief User information DTO
     *
     * @details
     * Populated from the "user" object inside register/login responses.
     * Storage values are in bytes; createdAt is an ISO-8601 string.
     */
    struct UserDto {
        quint64 id{};
        QString username;
        QString email;
        QString nickname;
        quint64 storageQuota{}; ///< Total allocated storage in bytes
        quint64 storageUsed{};  ///< Currently consumed storage in bytes
        QString createdAt;      ///< Account creation timestamp (ISO-8601)
    };

    /**
     * @brief Register endpoint result DTO
     *
     * @details
     * Wraps the UserDto returned after a successful registration.
     */
    struct RegisterResultDto {
        UserDto user;
    };

    /**
     * @brief Login endpoint result DTO
     *
     * @details
     * Contains the token pair and the authenticated user's profile.
     */
    struct LoginResultDto {
        QString accessToken;
        QString refreshToken;
        QString tokenType;
        int expiresIn{}; ///< Access-token lifetime in seconds (e.g. 7200)
        UserDto user;
    };

    /**
     * @brief Token-refresh endpoint result DTO
     *
     * @details
     * Contains a new token pair issued in exchange for the consumed refresh token.
     */
    struct RefreshResultDto {
        QString accessToken;
        QString refreshToken;
        int expiresIn{}; ///< New access-token lifetime in seconds
    };

    /**
     * @brief Uniform JSON envelope: { "code": 0, "message": "success", "data": ... }
     *
     * @details
     * All API responses are wrapped in this envelope.
     * The `data` field holds QJsonValue::Null when absent or explicitly null.
     */
    struct ApiEnvelope {
        int code{};
        QString message;
        QJsonValue data; ///< Payload; QJsonValue::Null when absent / null
    };

    // ---------------------------------------------------------------------------
    // Pure-function parsers – return std::nullopt on missing / wrong-type fields
    // ---------------------------------------------------------------------------

    /**
     * @brief Parse a raw JSON document into an ApiEnvelope.
     *
     * @details
     * Expected shape: { "code": <int>, "message": "<string>", "data": <any> }
     *
     * @param doc  Parsed QJsonDocument from the HTTP response body.
     * @return     Populated ApiEnvelope, or std::nullopt if the document is not
     *             an object or if "code" / "message" fields are absent.
     */
    inline auto ParseEnvelope(const QJsonDocument& doc) -> std::optional<ApiEnvelope> {
        if (!doc.isObject()) {
            return std::nullopt;
        }

        const QJsonObject obj = doc.object();
        if (!obj.contains(QLatin1String("code")) ||
            !obj.contains(QLatin1String("message"))) {
            return std::nullopt;
        }

        ApiEnvelope env;
        env.code = obj.value(QLatin1String("code")).toInt();
        env.message = obj.value(QLatin1String("message")).toString();
        env.data = obj.value(QLatin1String("data")); // QJsonValue::Undefined → Null
        return env;
    }

    /**
     * @brief Parse a JSON object into a UserDto.
     *
     * @details
     * Expected shape:
     * {
     *   "id": <number>, "username": "<string>", "email": "<string>",
     *   "nickname": "<string>", "storage_quota": <number>,
     *   "storage_used": <number>, "created_at": "<string>"
     * }
     *
     * @param obj  JSON object containing the user fields.
     * @return     Populated UserDto, or std::nullopt if the object is empty or
     *             "username" is missing.
     */
    inline auto ParseUserDto(const QJsonObject& obj) -> std::optional<UserDto> {
        if (obj.isEmpty()) {
            return std::nullopt;
        }

        UserDto user;
        user.id = static_cast<quint64>(obj.value(QLatin1String("id")).toDouble());
        user.username = obj.value(QLatin1String("username")).toString();
        user.email = obj.value(QLatin1String("email")).toString();
        user.nickname = obj.value(QLatin1String("nickname")).toString();
        user.storageQuota = static_cast<quint64>(obj.value(QLatin1String("storage_quota")).toDouble());
        user.storageUsed = static_cast<quint64>(obj.value(QLatin1String("storage_used")).toDouble());
        user.createdAt = obj.value(QLatin1String("created_at")).toString();

        if (user.username.isEmpty()) {
            return std::nullopt;
        }

        return user;
    }

    /**
     * @brief Parse register result from envelope data value.
     *
     * @details
     * Expected shape: data = { "user": { ... } }
     *
     * @param dataVal  The `data` field extracted from ApiEnvelope.
     * @return         Populated RegisterResultDto, or std::nullopt on missing /
     *                 wrong-type fields.
     */
    inline auto ParseRegisterResult(const QJsonValue& dataVal) -> std::optional<RegisterResultDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }

        const QJsonObject dataObj = dataVal.toObject();
        const QJsonValue userVal = dataObj.value(QLatin1String("user"));
        if (!userVal.isObject()) {
            return std::nullopt;
        }

        auto userDto = ParseUserDto(userVal.toObject());
        if (!userDto) {
            return std::nullopt;
        }

        RegisterResultDto result;
        result.user = std::move(*userDto);
        return result;
    }

    /**
     * @brief Parse login result from envelope data value.
     *
     * @details
     * Expected shape: data = { "access_token": "...", "refresh_token": "...",
     *                          "token_type": "Bearer", "expires_in": 7200,
     *                          "user": { ... } }
     *
     * @param dataVal  The `data` field extracted from ApiEnvelope.
     * @return         Populated LoginResultDto, or std::nullopt on missing /
     *                 wrong-type fields.
     */
    inline auto ParseLoginResult(const QJsonValue& dataVal) -> std::optional<LoginResultDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }

        const QJsonObject obj = dataVal.toObject();

        const QString accessToken = obj.value(QLatin1String("access_token")).toString();
        if (accessToken.isEmpty()) {
            return std::nullopt;
        }

        const QJsonValue userVal = obj.value(QLatin1String("user"));
        if (!userVal.isObject()) {
            return std::nullopt;
        }

        auto userDto = ParseUserDto(userVal.toObject());
        if (!userDto) {
            return std::nullopt;
        }

        LoginResultDto result;
        result.accessToken = accessToken;
        result.refreshToken = obj.value(QLatin1String("refresh_token")).toString();
        result.tokenType = obj.value(QLatin1String("token_type")).toString();
        result.expiresIn = obj.value(QLatin1String("expires_in")).toInt();
        result.user = std::move(*userDto);
        return result;
    }

    /**
     * @brief Parse refresh result from envelope data value.
     *
     * @details
     * Expected shape: data = { "access_token": "...", "refresh_token": "...",
     *                          "expires_in": 7200 }
     *
     * @param dataVal  The `data` field extracted from ApiEnvelope.
     * @return         Populated RefreshResultDto, or std::nullopt on missing /
     *                 wrong-type fields.
     */
    inline auto ParseRefreshResult(const QJsonValue& dataVal) -> std::optional<RefreshResultDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }

        const QJsonObject obj = dataVal.toObject();

        const QString accessToken = obj.value(QLatin1String("access_token")).toString();
        if (accessToken.isEmpty()) {
            return std::nullopt;
        }

        RefreshResultDto result;
        result.accessToken = accessToken;
        result.refreshToken = obj.value(QLatin1String("refresh_token")).toString();
        result.expiresIn = obj.value(QLatin1String("expires_in")).toInt();
        return result;
    }

} // namespace disk::qml::models
