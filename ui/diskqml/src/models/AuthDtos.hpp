#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <optional>

namespace disk::qml::models {

    // ---------------------------------------------------------------------------
    // Plain data structs (no Q_OBJECT / Q_PROPERTY)
    // ---------------------------------------------------------------------------

    struct UserDto {
        quint64 id{};
        QString username;
        QString email;
        QString nickname;
        quint64 storageQuota{};
        quint64 storageUsed{};
        QString createdAt;
    };

    struct RegisterResultDto {
        UserDto user;
    };

    struct LoginResultDto {
        QString accessToken;
        QString refreshToken;
        QString tokenType;
        int expiresIn{};
        UserDto user;
    };

    struct RefreshResultDto {
        QString accessToken;
        QString refreshToken;
        int expiresIn{};
    };

    /// Uniform JSON envelope: { "code": 0, "message": "success", "data": ... }
    struct ApiEnvelope {
        int code{};
        QString message;
        QJsonValue data; // QJsonValue::Null when absent / null
    };

    // ---------------------------------------------------------------------------
    // Pure-function parsers – return std::nullopt on missing / wrong-type fields
    // ---------------------------------------------------------------------------

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

    /// Parse register result from envelope data value.
    /// Expected shape: data = { "user": { ... } }
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

    /// Parse login result from envelope data value.
    /// Expected shape: data = { "access_token": "...", "refresh_token": "...",
    ///                          "token_type": "Bearer", "expires_in": 7200,
    ///                          "user": { ... } }
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

    /// Parse refresh result from envelope data value.
    /// Expected shape: data = { "access_token": "...", "refresh_token": "...",
    ///                          "expires_in": 7200 }
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
