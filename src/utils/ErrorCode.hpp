/**
 * @file ErrorCode.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 系统错误码枚举定义及 Result 类型
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <expected>
#include <unordered_map>
#include <utility>

#include <drogon/HttpTypes.h>

namespace disk::error {

    /**
     * @brief 系统错误码枚举
     *
     * 错误码规则：
     * - 0: 成功
     * - 10xxx: 通用错误
     * - 40xxx: 认证相关错误
     * - 50xxx: 文件相关错误
     * - 60xxx: 分享相关错误
     * - 70xxx: Redis相关错误
     */
    enum class Code : std::uint32_t {
        // ==================== 成功 ====================
        Success = 0,

        // ==================== 通用错误码 (10xxx) ====================
        /// 请求参数错误
        InvalidParameter = 10001,
        /// 参数校验失败
        ValidationFailed = 10002,
        /// 资源不存在
        ResourceNotFound = 10003,
        /// 资源冲突
        ResourceConflict = 10004,
        /// 请求过于频繁
        TooManyRequests = 10005,
        /// 服务器内部错误
        InternalError = 10006,

        // ==================== 认证错误码 (40xxx) ====================
        /// 用户名已存在
        UsernameExists = 40001,
        /// 邮箱已存在
        EmailExists = 40002,
        /// 参数格式不正确
        InvalidFormat = 40003,
        /// 用户不存在
        UserNotFound = 40100,
        /// 用户名或密码错误
        InvalidCredentials = 40101,
        /// 账户已锁定
        AccountLocked = 40102,
        /// 账户已禁用
        AccountDisabled = 40103,
        /// 令牌无效或已过期
        InvalidToken = 40104,
        /// 刷新令牌无效
        InvalidRefreshToken = 40105,
        /// 未提供令牌
        TokenMissing = 40106,
        /// 令牌格式错误
        TokenMalformed = 40107,
        /// 令牌已过期
        TokenExpired = 40108,
        /// 令牌类型错误（需要 access token）
        TokenWrongType = 40109,
        /// 刷新令牌已被使用
        RefreshTokenAlreadyUsed = 40110,
        /// 令牌已被注销
        TokenRevoked = 40111,

        // ==================== 文件错误码 (50xxx) ====================
        /// 文件名无效
        InvalidFilename = 50001,
        /// 文件类型不允许
        FileTypeNotAllowed = 50002,
        /// 文件大小超出限制
        FileSizeExceeded = 50003,
        /// 存储空间不足
        StorageQuotaExceeded = 50004,
        /// 文件不存在
        FileNotFound = 50005,
        /// 文件夹不存在
        FolderNotFound = 50006,
        /// 同名文件已存在
        FileAlreadyExists = 50007,
        /// 上传任务不存在或已过期
        UploadTaskNotFound = 50008,
        /// 分片校验失败
        ChunkVerifyFailed = 50009,
        /// 同名文件夹已存在
        FolderAlreadyExists = 50010,
        /// 文件读取失败
        FileReadError = 50011,

        // ==================== 分享错误码 (60xxx) ====================
        /// 分享不存在
        ShareNotFound = 60001,
        /// 分享已过期
        ShareExpired = 60002,
        /// 分享密码错误
        SharePasswordError = 60003,
        /// 无权限访问
        ShareAccessDenied = 60004,

        // ==================== Redis错误码 (70xxx) ====================
        /// Redis连接失败
        RedisConnectionFailed = 70001,
        /// Redis操作失败
        RedisOperationFailed = 70002,
        /// Redis key不存在
        RedisKeyNotFound = 70003,
    };

    // ==================== 错误码信息 ====================
    /**
     * @brief 获取错误码对应的 HTTP 状态码
     * @param code 错误码
     * @return HTTP 状态码
     */
    inline auto GetHttpStatus(Code code) -> drogon::HttpStatusCode {
        static const std::unordered_map<Code, drogon::HttpStatusCode> status_map = {
            // 成功
            {                 Code::Success,                  drogon::k200OK },

            // 通用错误
            {        Code::InvalidParameter,          drogon::k400BadRequest },
            {        Code::ValidationFailed,          drogon::k400BadRequest },
            {        Code::ResourceNotFound,            drogon::k404NotFound },
            {        Code::ResourceConflict,            drogon::k409Conflict },
            {         Code::TooManyRequests,     drogon::k429TooManyRequests },
            {           Code::InternalError, drogon::k500InternalServerError },

            // 认证错误
            {          Code::UsernameExists,          drogon::k400BadRequest },
            {             Code::EmailExists,          drogon::k400BadRequest },
            {           Code::InvalidFormat,          drogon::k400BadRequest },
            {            Code::UserNotFound,            drogon::k404NotFound },
            {      Code::InvalidCredentials,        drogon::k401Unauthorized },
            {           Code::AccountLocked,        drogon::k401Unauthorized },
            {         Code::AccountDisabled,        drogon::k401Unauthorized },
            {            Code::InvalidToken,        drogon::k401Unauthorized },
            {            Code::TokenMissing,        drogon::k401Unauthorized },
            {          Code::TokenMalformed,        drogon::k401Unauthorized },
            {            Code::TokenExpired,        drogon::k401Unauthorized },
            {          Code::TokenWrongType,        drogon::k401Unauthorized },
            {     Code::InvalidRefreshToken,        drogon::k401Unauthorized },
            { Code::RefreshTokenAlreadyUsed,        drogon::k401Unauthorized },
            {            Code::TokenRevoked,        drogon::k401Unauthorized },

            // 文件错误
            {         Code::InvalidFilename,          drogon::k400BadRequest },
            {      Code::FileTypeNotAllowed,          drogon::k400BadRequest },
            {        Code::FileSizeExceeded,          drogon::k400BadRequest },
            {    Code::StorageQuotaExceeded,          drogon::k400BadRequest },
            {            Code::FileNotFound,            drogon::k404NotFound },
            {          Code::FolderNotFound,            drogon::k404NotFound },
            {       Code::FileAlreadyExists,            drogon::k409Conflict },
            {      Code::UploadTaskNotFound,          drogon::k400BadRequest },
            {       Code::ChunkVerifyFailed,          drogon::k400BadRequest },
            {     Code::FolderAlreadyExists,            drogon::k409Conflict },
            {           Code::FileReadError, drogon::k500InternalServerError },

            // 分享错误
            {           Code::ShareNotFound,            drogon::k404NotFound },
            {            Code::ShareExpired,          drogon::k400BadRequest },
            {      Code::SharePasswordError,          drogon::k400BadRequest },
            {       Code::ShareAccessDenied,           drogon::k403Forbidden },

            // Redis错误
            {   Code::RedisConnectionFailed, drogon::k500InternalServerError },
            {    Code::RedisOperationFailed, drogon::k500InternalServerError },
            {        Code::RedisKeyNotFound,            drogon::k404NotFound },
        };

        auto it = status_map.find(code);
        if (it != status_map.end()) {
            return it->second;
        }
        return drogon::k500InternalServerError;
    }

    /**
     * @brief 获取错误码对应的默认消息
     * @param code 错误码
     * @return 错误消息
     */
    inline auto GetErrorMessage(Code code) -> std::string {
        static const std::unordered_map<Code, std::string> message_map = {
            // 成功
            {                 Code::Success,                                "success" },

            // 通用错误
            {        Code::InvalidParameter,             "Invalid request parameters" },
            {        Code::ValidationFailed,            "Parameter validation failed" },
            {        Code::ResourceNotFound,                     "Resource not found" },
            {        Code::ResourceConflict,                      "Resource conflict" },
            {         Code::TooManyRequests,                      "Too many requests" },
            {           Code::InternalError,                  "Internal server error" },

            // 认证错误
            {          Code::UsernameExists,            "Username already registered" },
            {             Code::EmailExists,               "Email already registered" },
            {           Code::InvalidFormat,               "Invalid parameter format" },
            {            Code::UserNotFound,                         "User not found" },
            {      Code::InvalidCredentials,           "Invalid username or password" },
            {           Code::AccountLocked, "Account locked, please try again later" },
            {         Code::AccountDisabled,              "Account has been disabled" },
            {            Code::InvalidToken,               "Token invalid or expired" },
            {            Code::TokenMissing,                     "Token not provided" },
            {          Code::TokenMalformed,                     "Token format error" },
            {            Code::TokenExpired,                          "Token expired" },
            {          Code::TokenWrongType,                       "Token type error" },
            {     Code::InvalidRefreshToken,                  "Invalid refresh token" },
            { Code::RefreshTokenAlreadyUsed,             "Refresh token already used" },
            {            Code::TokenRevoked,                          "Token revoked" },

            // 文件错误
            {         Code::InvalidFilename,                       "Invalid filename" },
            {      Code::FileTypeNotAllowed,                  "File type not allowed" },
            {        Code::FileSizeExceeded,                "File size exceeds limit" },
            {    Code::StorageQuotaExceeded,             "Insufficient storage space" },
            {            Code::FileNotFound,                         "File not found" },
            {          Code::FolderNotFound,                       "Folder not found" },
            {       Code::FileAlreadyExists,     "File with same name already exists" },
            {      Code::UploadTaskNotFound,       "Upload task not found or expired" },
            {       Code::ChunkVerifyFailed,              "Chunk verification failed" },
            {     Code::FolderAlreadyExists,   "Folder with same name already exists" },
            {           Code::FileReadError,                       "File read failed" },

            // 分享错误
            {           Code::ShareNotFound,                        "Share not found" },
            {            Code::ShareExpired,                          "Share expired" },
            {      Code::SharePasswordError,                   "Share password error" },
            {       Code::ShareAccessDenied,                          "Access denied" },

            // Redis错误
            {   Code::RedisConnectionFailed,                "Redis connection failed" },
            {    Code::RedisOperationFailed,                 "Redis operation failed" },
            {        Code::RedisKeyNotFound,                    "Redis key not found" },
        };

        auto it = message_map.find(code);
        if (it != message_map.end()) {
            return it->second;
        }
        return "Unknown error";
    }

    /**
     * @brief 获取错误码的整数值
     * @param code 错误码
     * @return 整数值
     */
    inline auto ToInt(Code code) -> std::uint32_t {
        return static_cast<std::uint32_t>(code);
    }

    /**
     * @brief 判断是否为成功状态
     * @param code 错误码
     * @return 是否成功
     */
    inline auto IsSuccess(Code code) -> bool {
        return code == Code::Success;
    }

    /**
     * @brief 判断是否为客户端错误 (4xx)
     * @param code 错误码
     * @return 是否为客户端错误
     */
    inline auto IsClientError(Code code) -> bool {
        auto status = GetHttpStatus(code);
        return status >= drogon::k400BadRequest && status < drogon::k500InternalServerError;
    }

    /**
     * @brief 判断是否为服务器错误 (5xx)
     * @param code 错误码
     * @return 是否为服务器错误
     */
    inline auto IsServerError(Code code) -> bool {
        auto status = GetHttpStatus(code);
        return status >= drogon::k500InternalServerError;
    }

    // ==================== ErrorInfo 结构体定义 ====================

    /**
     * @brief 错误信息结构体，包含错误码和详细消息
     *
     * 使用示例：
     * @code
     * // 仅使用错误码（使用默认消息）
     * return std::unexpected(ErrorInfo(Code::InvalidParameter));
     *
     * // 使用错误码和自定义详细消息
     * return std::unexpected(ErrorInfo(Code::ValidationFailed, "参数 'age' 必须是正整数"));
     * @endcode
     */
    struct ErrorInfo {
        Code code;
        std::string message;

        /// 仅使用错误码构造，使用默认消息
        explicit ErrorInfo(Code c) : code(c), message(GetErrorMessage(c)) {}

        /// 使用错误码和自定义消息构造
        ErrorInfo(Code c, std::string msg) : code(c), message(std::move(msg)) {}

        /// 获取 HTTP 状态码
        [[nodiscard]]
        auto HttpStatus() const noexcept -> drogon::HttpStatusCode {
            return GetHttpStatus(code);
        }

        /// 获取错误码整数值
        [[nodiscard]]
        auto CodeInt() const noexcept -> std::uint32_t {
            return ToInt(code);
        }
    };

    // ==================== Result 类型定义 ====================

    /**
     * @brief 通用结果类型，用于函数返回值
     * @tparam T 成功时的返回值类型
     *
     * 使用示例：
     * @code
     * auto GetUser(int id) -> Result<User> {
     *     if (id <= 0) {
     *         return std::unexpected(ErrorInfo(Code::InvalidParameter, "用户 ID 必须为正整数"));
     *     }
     *     return user;
     * }
     * @endcode
     */
    template <typename T>
    using Result = std::expected<T, ErrorInfo>;

    /**
     * @brief 无返回值的结果类型
     *
     * 使用示例：
     * @code
     * auto DeleteUser(int id) -> Result<void> {
     *     if (id <= 0) {
     *         return std::unexpected(ErrorInfo(Code::InvalidParameter));
     *     }
     *     // ... 删除操作
     *     return {};
     * }
     * @endcode
     */

    /**
     * @brief 将一种错误类型的 Result 转换为另一种类型（保留错误信息）
     * @tparam T 目标类型
     * @tparam U 源类型
     * @param result 源 Result
     * @return 转换后的 Result（仅当源为错误时有效）
     */
    template <typename T, typename U>
    [[nodiscard]]
    auto PropagateError(const Result<U>& result) -> Result<T> {
        return std::unexpected(result.error());
    }

} // namespace disk::error

namespace Error = disk::error;
using ErrorCode = Error::Code;
using ErrorInfo = Error::ErrorInfo;
template <typename T>
using Result = Error::Result<T>;
