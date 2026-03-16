/**
 * @file ApiEnvelope.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QML 客户端共享 API 响应信封
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <functional>
#include <optional>

namespace disk::qml::models {

    // ==================== API 信封 ====================

    /**
     * @brief 统一 JSON 信封格式：{ "code": 0, "message": "success", "data": ... }
     *
     * @details
     * 所有 API 响应都使用此信封格式包装。
     * 当 `data` 字段缺失或显式为 null 时，其值为 QJsonValue::Null。
     *
     * 镜像后端 src/utils/Response.hpp 中定义的结构：
     * - code 0 = 成功
     * - code 10xxx = 通用错误
     * - code 40xxx = 认证错误
     * - code 50xxx = 文件错误
     * - code 60xxx = 分享错误
     */
    struct ApiEnvelope {
        int code{};
        QString message;
        QJsonValue data; ///< 数据负载；缺失或为 null 时值为 QJsonValue::Null

        /// @brief 检查信封是否表示成功响应（code == 0）
        [[nodiscard]] auto IsSuccess() const noexcept -> bool { return code == 0; }

        /// @brief 检查信封是否表示错误响应（code != 0）
        [[nodiscard]] auto IsError() const noexcept -> bool { return code != 0; }
    };

    // ==================== 信封解析 ====================

    /**
     * @brief 将原始 JSON 文档解析为 ApiEnvelope
     *
     * @details
     * 预期格式：{ "code": <int>, "message": "<string>", "data": <any> }
     *
     * @param doc  从 HTTP 响应体解析的 QJsonDocument
     * @return     填充后的 ApiEnvelope；若文档不是对象或缺少 "code" / "message" 字段则返回 std::nullopt
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
        env.data = obj.value(QLatin1String("data")); // QJsonValue::Undefined 转换为 Null
        return env;
    }

    /**
     * @brief 将原始字节数组解析为 ApiEnvelope
     *
     * @details
     * 便捷重载，处理从原始字节进行 JSON 反序列化。
     *
     * @param body  HTTP 响应的原始字节数组
     * @return      填充后的 ApiEnvelope；若 body 为空、无法解析为 JSON 或缺少信封字段则返回 std::nullopt
     */
    inline auto ParseEnvelope(const QByteArray& body) -> std::optional<ApiEnvelope> {
        if (body.isEmpty()) {
            return std::nullopt;
        }

        QJsonParseError parseError;
        const QJsonDocument json = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            return std::nullopt;
        }

        return ParseEnvelope(json);
    }

    // ==================== 类型化数据提取 ====================

    /**
     * @brief 从信封中提取 `data` 字段作为 QJsonObject
     *
     * @param env  要提取数据的 API 信封
     * @return     数据对象；若 `data` 不是 JSON 对象则返回 std::nullopt
     */
    inline auto EnvelopeDataObject(const ApiEnvelope& env) -> std::optional<QJsonObject> {
        if (!env.data.isObject()) {
            return std::nullopt;
        }
        return env.data.toObject();
    }

    // ==================== 通用回调类型 ====================

    /**
     * @brief API 端点的通用回调类型
     *
     * @details
     * - 成功时：`envelope` 已填充，`networkError` 为空
     * - 网络错误时：`envelope` 为默认构造，`networkError` 描述问题
     *
     * 所有 API 模块（AuthApi、FileApi、FolderApi 等）应使用此回调类型
     */
    using ApiCallback = std::function<void(ApiEnvelope envelope, QString networkError)>;

    // ==================== 响应 → 信封辅助函数 ====================

    /**
     * @brief 将原始 HTTP 响应解析为 ApiEnvelope 并调用回调
     *
     * @details
     * 共享辅助函数，集中处理字节数组 → 信封的解析流程。
     * 处理网络错误、空响应体、JSON 解析失败和无效信封。
     * 适用于任何 API 模块的响应 lambda 内部使用。
     *
     * @param hasNetworkError      是否发生网络层错误
     * @param networkErrorString   网络错误的可读描述
     * @param body                 原始响应体字节
     * @param cb                   结果回调
     */
    inline auto ParseEnvelopeFromReply(
        bool hasNetworkError,
        const QString& networkErrorString,
        const QByteArray& body,
        ApiCallback& cb
    ) -> void {
        if (hasNetworkError) {
            cb(ApiEnvelope{}, networkErrorString);
            return;
        }

        auto envelope = ParseEnvelope(body);
        if (!envelope) {
            cb(ApiEnvelope{}, QStringLiteral("Failed to parse response JSON"));
            return;
        }

        cb(std::move(*envelope), QString{});
    }

} // namespace disk::qml::models
