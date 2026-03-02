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

    auto ErrorCodeFromInt(int code) -> std::optional<ErrorCode>;
    auto ToUserMessage(int code, const QString& fallbackServerMessage) -> QString;

} // namespace disk::qml::utils
