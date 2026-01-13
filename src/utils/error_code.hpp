/**
 * @file error_code.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 系统错误码枚举定义及 Result 类型
 * @version 0.1
 * @date 2026-01-13
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <expected>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <drogon/HttpTypes.h>

namespace disk ::error {

    /**
     * @brief 系统错误码枚举
     *
     * 错误码规则：
     * - 0: 成功
     * - 10xxx: 通用错误
     * - 40xxx: 认证相关错误
     * - 50xxx: 文件相关错误
     * - 60xxx: 分享相关错误
     */
    enum class ErrorCode : std::uint16_t {
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

        // ==================== 分享错误码 (60xxx) ====================
        /// 分享不存在
        ShareNotFound = 60001,
        /// 分享已过期
        ShareExpired = 60002,
        /// 分享密码错误
        SharePasswordError = 60003,
        /// 无权限访问
        ShareAccessDenied = 60004,
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
     *         return std::unexpected(ErrorCode::InvalidParameter);
     *     }
     *     return user;
     * }
     * @endcode
     */
    template <typename T>
    using Result = std::expected<T, ErrorCode>;

    /**
     * @brief 无返回值的结果类型
     *
     * 使用示例：
     * @code
     * auto DeleteUser(int id) -> VoidResult {
     *     if (id <= 0) {
     *         return std::unexpected(ErrorCode::InvalidParameter);
     *     }
     *     // ... 删除操作
     *     return {};
     * }
     * @endcode
     */
    using VoidResult = Result<void>;

    /**
     * @brief 创建错误结果的便捷函数
     * @tparam T 期望的返回值类型
     * @param code 错误码
     * @return 包含错误的 Result
     *
     * 使用示例：
     * @code
     * return MakeError<User>(ErrorCode::ResourceNotFound);
     * @endcode
     */
    template <typename T>
    [[nodiscard]]
    constexpr auto MakeError(ErrorCode code) -> Result<T> {
        return std::unexpected(code);
    }

    /**
     * @brief 创建成功结果的便捷函数
     * @tparam T 返回值类型
     * @param value 成功的值
     * @return 包含值的 Result
     *
     * 使用示例：
     * @code
     * return MakeSuccess(user);
     * @endcode
     */
    template <typename T>
    [[nodiscard]]
    constexpr auto MakeSuccess(T&& value) -> Result<std::decay_t<T>> {
        return std::forward<T>(value);
    }

    /**
     * @brief 创建无返回值的成功结果
     * @return 成功的 VoidResult
     */
    [[nodiscard]]
    constexpr auto MakeSuccess() -> VoidResult {
        return {};
    }

    /**
     * @brief 将一种错误类型的 Result 转换为另一种类型（保留错误码）
     * @tparam T 目标类型
     * @tparam U 源类型
     * @param result 源 Result
     * @return 转换后的 Result（仅当源为错误时有效）
     */
    template <typename T, typename U>
    [[nodiscard]]
    constexpr auto PropagateError(const Result<U>& result) -> Result<T> {
        return std::unexpected(result.error());
    }

    // ==================== 错误码信息 ====================

    /**
     * @brief 错误码信息结构体
     */
    struct ErrorInfo {
        drogon::HttpStatusCode http_status;
        std::string_view message;
    };

    /**
     * @brief 获取错误码对应的 HTTP 状态码
     * @param code 错误码
     * @return HTTP 状态码
     */
    inline auto GetHttpStatus(ErrorCode code) -> drogon::HttpStatusCode {
        static const std::unordered_map<ErrorCode, drogon::HttpStatusCode> status_map = {
            // 成功
            {              ErrorCode::Success,                  drogon::k200OK },

            // 通用错误
            {     ErrorCode::InvalidParameter,          drogon::k400BadRequest },
            {     ErrorCode::ValidationFailed,          drogon::k400BadRequest },
            {     ErrorCode::ResourceNotFound,            drogon::k404NotFound },
            {     ErrorCode::ResourceConflict,            drogon::k409Conflict },
            {      ErrorCode::TooManyRequests,     drogon::k429TooManyRequests },
            {        ErrorCode::InternalError, drogon::k500InternalServerError },

            // 认证错误
            {       ErrorCode::UsernameExists,          drogon::k400BadRequest },
            {          ErrorCode::EmailExists,          drogon::k400BadRequest },
            {        ErrorCode::InvalidFormat,          drogon::k400BadRequest },
            {   ErrorCode::InvalidCredentials,        drogon::k401Unauthorized },
            {        ErrorCode::AccountLocked,        drogon::k401Unauthorized },
            {      ErrorCode::AccountDisabled,        drogon::k401Unauthorized },
            {         ErrorCode::InvalidToken,        drogon::k401Unauthorized },
            {  ErrorCode::InvalidRefreshToken,        drogon::k401Unauthorized },

            // 文件错误
            {      ErrorCode::InvalidFilename,          drogon::k400BadRequest },
            {   ErrorCode::FileTypeNotAllowed,          drogon::k400BadRequest },
            {     ErrorCode::FileSizeExceeded,          drogon::k400BadRequest },
            { ErrorCode::StorageQuotaExceeded,          drogon::k400BadRequest },
            {         ErrorCode::FileNotFound,            drogon::k404NotFound },
            {       ErrorCode::FolderNotFound,            drogon::k404NotFound },
            {    ErrorCode::FileAlreadyExists,            drogon::k409Conflict },
            {   ErrorCode::UploadTaskNotFound,          drogon::k400BadRequest },
            {    ErrorCode::ChunkVerifyFailed,          drogon::k400BadRequest },

            // 分享错误
            {        ErrorCode::ShareNotFound,            drogon::k404NotFound },
            {         ErrorCode::ShareExpired,          drogon::k400BadRequest },
            {   ErrorCode::SharePasswordError,          drogon::k400BadRequest },
            {    ErrorCode::ShareAccessDenied,           drogon::k403Forbidden },
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
    inline auto GetErrorMessage(ErrorCode code) -> std::string_view {
        static const std::unordered_map<ErrorCode, std::string_view> message_map = {
            // 成功
            {              ErrorCode::Success,                "success" },

            // 通用错误
            {     ErrorCode::InvalidParameter,           "请求参数错误" },
            {     ErrorCode::ValidationFailed,           "参数校验失败" },
            {     ErrorCode::ResourceNotFound,             "资源不存在" },
            {     ErrorCode::ResourceConflict,               "资源冲突" },
            {      ErrorCode::TooManyRequests,           "请求过于频繁" },
            {        ErrorCode::InternalError,         "服务器内部错误" },

            // 认证错误
            {       ErrorCode::UsernameExists,         "用户名已被注册" },
            {          ErrorCode::EmailExists,           "邮箱已被注册" },
            {        ErrorCode::InvalidFormat,         "参数格式不正确" },
            {   ErrorCode::InvalidCredentials,       "用户名或密码错误" },
            {        ErrorCode::AccountLocked, "账户已锁定，请稍后重试" },
            {      ErrorCode::AccountDisabled,           "账户已被禁用" },
            {         ErrorCode::InvalidToken,       "令牌无效或已过期" },
            {  ErrorCode::InvalidRefreshToken,           "刷新令牌无效" },

            // 文件错误
            {      ErrorCode::InvalidFilename,             "文件名无效" },
            {   ErrorCode::FileTypeNotAllowed,         "文件类型不允许" },
            {     ErrorCode::FileSizeExceeded,       "文件大小超出限制" },
            { ErrorCode::StorageQuotaExceeded,           "存储空间不足" },
            {         ErrorCode::FileNotFound,             "文件不存在" },
            {       ErrorCode::FolderNotFound,           "文件夹不存在" },
            {    ErrorCode::FileAlreadyExists,         "同名文件已存在" },
            {   ErrorCode::UploadTaskNotFound, "上传任务不存在或已过期" },
            {    ErrorCode::ChunkVerifyFailed,           "分片校验失败" },

            // 分享错误
            {        ErrorCode::ShareNotFound,             "分享不存在" },
            {         ErrorCode::ShareExpired,             "分享已过期" },
            {   ErrorCode::SharePasswordError,           "分享密码错误" },
            {    ErrorCode::ShareAccessDenied,             "无权限访问" },
        };

        auto it = message_map.find(code);
        if (it != message_map.end()) {
            return it->second;
        }
        return "未知错误";
    }

    /**
     * @brief 获取错误码的完整信息
     * @param code 错误码
     * @return 错误信息结构体
     */
    inline auto GetErrorInfo(ErrorCode code) -> ErrorInfo {
        return {
            .http_status = GetHttpStatus(code),
            .message = GetErrorMessage(code)
        };
    }

    /**
     * @brief 获取错误码的整数值
     * @param code 错误码
     * @return 整数值
     */
    inline auto ToInt(ErrorCode code) -> std::uint16_t {
        return static_cast<std::uint16_t>(code);
    }

    /**
     * @brief 判断是否为成功状态
     * @param code 错误码
     * @return 是否成功
     */
    inline auto IsSuccess(ErrorCode code) -> bool {
        return code == ErrorCode::Success;
    }

    /**
     * @brief 判断是否为客户端错误 (4xx)
     * @param code 错误码
     * @return 是否为客户端错误
     */
    inline auto IsClientError(ErrorCode code) -> bool {
        auto status = GetHttpStatus(code);
        return status >= drogon::k400BadRequest && status < drogon::k500InternalServerError;
    }

    /**
     * @brief 判断是否为服务器错误 (5xx)
     * @param code 错误码
     * @return 是否为服务器错误
     */
    inline auto IsServerError(ErrorCode code) -> bool {
        auto status = GetHttpStatus(code);
        return status >= drogon::k500InternalServerError;
    }

} // namespace disk::error
