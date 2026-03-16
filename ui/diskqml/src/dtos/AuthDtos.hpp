/**
 * @file AuthDtos.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QML 客户端认证请求/响应数据传输对象及 JSON 解析辅助函数
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <optional>

#include <dtos/ApiEnvelope.hpp>

namespace disk::qml::models {

    // ==================== 请求数据传输对象 ====================

    /**
     * @brief 注册请求数据传输对象
     *
     * @details
     * ToJsonObject() 生成的 JSON 键：
     * - "username": 新账户的显示名称
     * - "email":    账户邮箱地址
     * - "password": 明文密码（通过 TLS 传输）
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
     * @brief 登录请求数据传输对象
     *
     * @details
     * ToJsonObject() 生成的 JSON 键：
     * - "account":  服务器接受的用户名或邮箱
     * - "password": 明文密码（通过 TLS 传输）
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
     * @brief 令牌刷新请求数据传输对象
     *
     * @details
     * ToJsonObject() 生成的 JSON 键：
     * - "refresh_token": 登录时获取的单次使用刷新令牌
     */
    struct RefreshTokenRequest {
        QString refreshToken;

        [[nodiscard]] auto ToJsonObject() const -> QJsonObject {
            QJsonObject obj;
            obj.insert(QLatin1String("refresh_token"), refreshToken);
            return obj;
        }
    };

    // ==================== 响应数据传输对象 ====================
    // ---------------------------------------------------------------------------
    // 纯数据结构体（无 Q_OBJECT / Q_PROPERTY）
    // ---------------------------------------------------------------------------

    /**
     * @brief 用户信息数据传输对象
     *
     * @details
     * 从注册/登录响应中的 "user" 对象填充。
     * 存储值以字节为单位；createdAt 为 ISO-8601 字符串。
     */
    struct UserDto {
        quint64 id{};
        QString username;
        QString email;
        QString nickname;
        quint64 storageQuota{}; ///< 总分配存储空间（字节）
        quint64 storageUsed{};  ///< 当前已用存储空间（字节）
        QString createdAt;      ///< 账户创建时间戳（ISO-8601）
    };

    /**
     * @brief 注册端点结果数据传输对象
     *
     * @details
     * 包装注册成功后返回的 UserDto。
     */
    struct RegisterResultDto {
        UserDto user;
    };

    /**
     * @brief 登录端点结果数据传输对象
     *
     * @details
     * 包含令牌对和已认证用户的资料。
     */
    struct LoginResultDto {
        QString accessToken;
        QString refreshToken;
        QString tokenType;
        int expiresIn{}; ///< 访问令牌有效期（秒），例如 7200
        UserDto user;
    };

    /**
     * @brief 令牌刷新端点结果数据传输对象
     *
     * @details
     * 包含用已消费的刷新令牌换取的新令牌对。
     */
    struct RefreshResultDto {
        QString accessToken;
        QString refreshToken;
        int expiresIn{}; ///< 新访问令牌有效期（秒）
    };

    // 注意：ApiEnvelope、ParseEnvelope、ParseEnvelopeFromReply 和 ApiCallback
    //       定义在 <dtos/ApiEnvelope.hpp> 中，所有 API 模块共享。

    /**
     * @brief 将 JSON 对象解析为 UserDto
     *
     * @details
     * 预期格式：
     * {
     *   "id": <number>, "username": "<string>", "email": "<string>",
     *   "nickname": "<string>", "storage_quota": <number>,
     *   "storage_used": <number>, "created_at": "<string>"
     * }
     *
     * @param obj  包含用户字段的 JSON 对象
     * @return     填充后的 UserDto；若对象为空或缺少 "username" 则返回 std::nullopt
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
     * @brief 从信封数据值解析注册结果
     *
     * @details
     * 预期格式：data = { "user": { ... } }
     *
     * @param dataVal  从 ApiEnvelope 提取的 `data` 字段
     * @return         填充后的 RegisterResultDto；若字段缺失或类型错误则返回 std::nullopt
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
     * @brief 从信封数据值解析登录结果
     *
     * @details
     * 预期格式：data = { "access_token": "...", "refresh_token": "...",
     *                          "token_type": "Bearer", "expires_in": 7200,
     *                          "user": { ... } }
     *
     * @param dataVal  从 ApiEnvelope 提取的 `data` 字段
     * @return         填充后的 LoginResultDto；若字段缺失或类型错误则返回 std::nullopt
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
     * @brief 从信封数据值解析刷新结果
     *
     * @details
     * 预期格式：data = { "access_token": "...", "refresh_token": "...",
     *                          "expires_in": 7200 }
     *
     * @param dataVal  从 ApiEnvelope 提取的 `data` 字段
     * @return         填充后的 RefreshResultDto；若字段缺失或类型错误则返回 std::nullopt
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
