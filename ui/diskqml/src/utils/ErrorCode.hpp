#pragma once

#include <QString>
#include <optional>

namespace disk::qml::utils {

    enum class ErrorCode : int {
        // Common
        Success = 0,
        InvalidParameter = 10001,
        ValidationFailed = 10002,
        ResourceNotFound = 10003,
        ResourceConflict = 10004,
        TooManyRequests = 10005,
        InternalError = 10006,

        // Auth
        UsernameExists = 40001,
        EmailExists = 40002,
        InvalidFormat = 40003,
        UserNotFound = 40100,
        InvalidCredentials = 40101,
        AccountLocked = 40102,
        AccountDisabled = 40103,
        InvalidToken = 40104,
        InvalidRefreshToken = 40105,
        TokenMissing = 40106,
        TokenMalformed = 40107,
        TokenExpired = 40108,
        TokenWrongType = 40109,
        RefreshTokenAlreadyUsed = 40110,
        TokenRevoked = 40111,
    };

    inline auto ErrorCodeFromInt(int code) -> std::optional<ErrorCode> {
        switch (code) {
            case 0    : return ErrorCode::Success;
            case 10001: return ErrorCode::InvalidParameter;
            case 10002: return ErrorCode::ValidationFailed;
            case 10003: return ErrorCode::ResourceNotFound;
            case 10004: return ErrorCode::ResourceConflict;
            case 10005: return ErrorCode::TooManyRequests;
            case 10006: return ErrorCode::InternalError;
            case 40001: return ErrorCode::UsernameExists;
            case 40002: return ErrorCode::EmailExists;
            case 40003: return ErrorCode::InvalidFormat;
            case 40100: return ErrorCode::UserNotFound;
            case 40101: return ErrorCode::InvalidCredentials;
            case 40102: return ErrorCode::AccountLocked;
            case 40103: return ErrorCode::AccountDisabled;
            case 40104: return ErrorCode::InvalidToken;
            case 40105: return ErrorCode::InvalidRefreshToken;
            case 40106: return ErrorCode::TokenMissing;
            case 40107: return ErrorCode::TokenMalformed;
            case 40108: return ErrorCode::TokenExpired;
            case 40109: return ErrorCode::TokenWrongType;
            case 40110: return ErrorCode::RefreshTokenAlreadyUsed;
            case 40111: return ErrorCode::TokenRevoked;
            default   : return std::nullopt;
        }
    }

    inline auto ToUserMessage(int code, const QString& fallbackServerMessage) -> QString {
        switch (code) {
            case 10002:
            case 40003: return QStringLiteral("参数格式不正确");
            case 10005: return QStringLiteral("请求过于频繁，请稍后再试");
            case 10006: return QStringLiteral("服务器错误，请稍后重试");
            case 40001: return QStringLiteral("用户名已被注册");
            case 40002: return QStringLiteral("邮箱已被注册");
            case 40101: return QStringLiteral("用户名或密码错误");
            case 40102: return QStringLiteral("账户已锁定，请15分钟后重试");
            case 40103: return QStringLiteral("账户已被禁用");
            case 40108: return QStringLiteral("令牌已过期");
            default:
                if (!fallbackServerMessage.isEmpty()) {
                    return fallbackServerMessage;
                }
                return QStringLiteral("未知错误");
        }
    }

} // namespace disk::qml::utils
