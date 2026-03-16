/**
 * @file ErrorCode.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QML 客户端错误码和面向用户的错误消息映射
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <QString>
#include <optional>

namespace disk::qml::utils {

    /**
     * @brief 强类型错误码，镜像后端 HTTP/业务错误码
     * @details 错误码范围：
     *   - 0       : 成功
     *   - 10xxx   : 通用错误（如参数无效、限流、内部错误）
     *   - 40xxx   : 认证错误（如用户不存在、凭证无效、令牌问题）
     *   - 50xxx   : 文件错误（如文件不存在、上传失败、配额超限）
     *   - 60xxx   : 分享错误（如分享不存在、已过期、密码错误）
     *   - 70xxx   : Redis 错误（服务端错误，显示为备用消息）
     */
    enum class ErrorCode : int {
        // 通用
        Success = 0,
        InvalidParameter = 10001,
        ValidationFailed = 10002,
        ResourceNotFound = 10003,
        ResourceConflict = 10004,
        TooManyRequests = 10005,
        InternalError = 10006,

        // 认证
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

        // 文件
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

        // 分享
        ShareNotFound = 60001,
        ShareExpired = 60002,
        SharePasswordError = 60003,
        ShareAccessDenied = 60004,

        // Redis（服务端错误，面向用户的备用消息）
        RedisConnectionFailed = 70001,
        RedisOperationFailed = 70002,
        RedisKeyNotFound = 70003,
    };

    /**
     * @brief 将原始整数错误码转换为对应的 ErrorCode 枚举值
     * @param code  从服务器响应接收的整数错误码
     * @return 若错误码被识别则返回对应的 ErrorCode；否则返回 std::nullopt
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
            // 文件
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
            // 分享
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
     * @brief 将错误码映射为本地化的面向用户的消息字符串
     * @details 对于已知错误码返回固定的中文消息。对于未识别的错误码，
     *          当 @p fallbackServerMessage 非空时使用其作为备用消息，
     *          否则返回"未知错误"。
     * @param code                  从服务器接收的整数错误码
     * @param fallbackServerMessage 可选的服务器提供的备用消息
     * @return 适合在 UI 中显示的 QString
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
            // 文件错误
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
            // 分享错误
            case 60001: return QStringLiteral("分享不存在");
            case 60002: return QStringLiteral("分享链接已过期");
            case 60003: return QStringLiteral("分享密码错误");
            case 60004: return QStringLiteral("无权限访问该分享");
            // Redis 错误（服务端错误，面向用户的备用消息）
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
