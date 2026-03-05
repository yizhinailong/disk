/**
 * @file ErrorCode.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QML client error codes and user-facing message mapping
 * @version 0.1
 * @date 2026-03-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <QString>
#include <optional>

namespace disk::qml::utils {

    /**
     * @brief Strongly-typed error codes mirroring backend HTTP/business error codes.
     * @details Code ranges:
     *   - 0       : Success
     *   - 10xxx   : Common errors (e.g., invalid parameter, rate limit, internal error)
     *   - 40xxx   : Auth errors (e.g., user not found, invalid credentials, token issues)
     *   - 50xxx   : File errors (e.g., file not found, upload failed, quota exceeded)
     *   - 60xxx   : Share errors (e.g., share not found, expired, wrong password)
     *   - 70xxx   : Redis errors (server-side, shown as fallback messages)
     */
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

        // File
        InvalidFilename = 50001,
        FileTypeNotAllowed = 50002,
        FileSizeExceeded = 50003,
        StorageQuotaExceeded = 50004,
        FileNotFound = 50005,
        FolderNotFound = 50006,
        FileAlreadyExists = 50007,
        UploadTaskNotFound = 50008,
        ChunkVerifyFailed = 50009,
        FolderAlreadyExists = 50010,
        FileReadError = 50011,

        // Share
        ShareNotFound = 60001,
        ShareExpired = 60002,
        SharePasswordError = 60003,
        ShareAccessDenied = 60004,

        // Redis (server-side, user-facing fallback)
        RedisConnectionFailed = 70001,
        RedisOperationFailed = 70002,
        RedisKeyNotFound = 70003,
    };

    /**
     * @brief Convert a raw integer error code into the corresponding ErrorCode enum value.
     * @param code  The integer error code received from the server response.
     * @return An optional ErrorCode if the code is recognised; std::nullopt otherwise.
     */
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
            // File
            case 50001: return ErrorCode::InvalidFilename;
            case 50002: return ErrorCode::FileTypeNotAllowed;
            case 50003: return ErrorCode::FileSizeExceeded;
            case 50004: return ErrorCode::StorageQuotaExceeded;
            case 50005: return ErrorCode::FileNotFound;
            case 50006: return ErrorCode::FolderNotFound;
            case 50007: return ErrorCode::FileAlreadyExists;
            case 50008: return ErrorCode::UploadTaskNotFound;
            case 50009: return ErrorCode::ChunkVerifyFailed;
            case 50010: return ErrorCode::FolderAlreadyExists;
            case 50011: return ErrorCode::FileReadError;
            // Share
            case 60001: return ErrorCode::ShareNotFound;
            case 60002: return ErrorCode::ShareExpired;
            case 60003: return ErrorCode::SharePasswordError;
            case 60004: return ErrorCode::ShareAccessDenied;
            // Redis
            case 70001: return ErrorCode::RedisConnectionFailed;
            case 70002: return ErrorCode::RedisOperationFailed;
            case 70003: return ErrorCode::RedisKeyNotFound;
            default   : return std::nullopt;
        }
    }

    /**
     * @brief Map an error code to a localised user-facing message string.
     * @details For well-known codes a fixed Chinese message is returned.  For
     *          unrecognised codes the function falls back to @p fallbackServerMessage
     *          when it is non-empty, and returns "未知错误" otherwise.
     * @param code                  The integer error code received from the server.
     * @param fallbackServerMessage Optional server-provided message used as fallback.
     * @return A QString suitable for display in the UI.
     */
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
            // File errors
            case 50001: return QStringLiteral("文件名无效");
            case 50002: return QStringLiteral("不支持该文件类型");
            case 50003: return QStringLiteral("文件大小超出限制");
            case 50004: return QStringLiteral("存储空间不足");
            case 50005: return QStringLiteral("文件不存在");
            case 50006: return QStringLiteral("文件夹不存在");
            case 50007: return QStringLiteral("同名文件已存在");
            case 50008: return QStringLiteral("上传任务不存在或已过期");
            case 50009: return QStringLiteral("分片校验失败，请重新上传");
            case 50010: return QStringLiteral("同名文件夹已存在");
            case 50011: return QStringLiteral("文件读取失败");
            // Share errors
            case 60001: return QStringLiteral("分享不存在");
            case 60002: return QStringLiteral("分享链接已过期");
            case 60003: return QStringLiteral("分享密码错误");
            case 60004: return QStringLiteral("无权限访问该分享");
            // Redis errors (server-side, user-facing fallback)
            case 70001:
            case 70002: return QStringLiteral("服务器错误，请稍后重试");
            case 70003: return QStringLiteral("资源不存在");
            default:
                if (!fallbackServerMessage.isEmpty()) {
                    return fallbackServerMessage;
                }
                return QStringLiteral("未知错误");
        }
    }

} // namespace disk::qml::utils
