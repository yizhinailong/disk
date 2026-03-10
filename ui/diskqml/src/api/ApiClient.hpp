/**
 * @file ApiClient.hpp
 * @brief Qt REST 客户端封装
 * @details 基于 Qt 6.8 QRestAccessManager + QNetworkRequestFactory 构建的 REST 客户端，供 QML 客户端使用
 * @author LiuFeng (liufeng.code@outlook.com)
 *
 * @copyright Copyright (c) 2026
 */
#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkRequestFactory>
#include <QObject>
#include <QRestAccessManager>
#include <QString>
#include <QUrlQuery>
#include <functional>

class QJsonObject;

namespace disk::qml::api {

    /**
     * @brief HTTP 请求完成时的回调函数
     *
     * @param hasNetworkError 是否发生网络层错误（未收到 HTTP 响应）
     * @param networkErrorString 网络错误的可读描述；成功时为空
     * @param httpStatus 服务器返回的 HTTP 状态码（hasNetworkError 为 true 时为 0）
     * @param body 原始响应体字节；错误时可能为空
     */
    using ApiReplyCallback = std::function<void(bool hasNetworkError, QString networkErrorString, int httpStatus, QByteArray body)>;

    /// @brief 为保持源码兼容性保留的旧别名。
    using PostJsonCallback = ApiReplyCallback;
    /**
     * @brief 基于 Qt 6.8 QRestAccessManager + QNetworkRequestFactory 构建的 REST 客户端
     *
     * 持有一个 QNetworkAccessManager 并用 QRestAccessManager 封装。
     * 所有请求共享一个公共基础 URL 和可选的 Bearer 令牌，
     * 通过 SetBaseUrl() 和 SetBearerToken() 配置。
     *
     * 每个请求方法都有一个接受显式 bearerToken 参数的变体。
     * 该令牌应用于工厂的本地副本，因此不会修改共享状态。
     */
    class ApiClient : public QObject {
        Q_OBJECT

    public:
        explicit ApiClient(QObject* parent = nullptr);

        /**
         * @brief 设置内部工厂创建的所有请求的基础 URL
         *
         * @param url 基础 URL，例如 "http://127.0.0.1:8080"
         */
        virtual auto SetBaseUrl(const QUrl& url) -> void;
        /**
         * @brief 设置应用于所有后续请求的 Bearer 令牌
         *
         * @param token 访问令牌字符串（不含 "Bearer " 前缀）
         */
        virtual auto SetBearerToken(const QString& token) -> void;

        // ==================== POST ====================

        /**
         * @brief 使用共享 Bearer 令牌向 @p path POST JSON 请求体
         */
        virtual auto PostJson(const QString& path, const QJsonObject& body, QObject* ctx, ApiReplyCallback cb) -> void;

        /**
         * @brief 使用调用者提供的 Bearer 令牌 POST JSON 请求体，不修改共享状态
         */
        virtual auto PostJsonWithBearerToken(
            const QString& path,
            const QJsonObject& body,
            const QString& bearerToken,
            QObject* ctx,
            ApiReplyCallback cb
        ) -> void;

        // ==================== POST Raw Bytes ====================

        /**
         * @brief 使用共享 Bearer 令牌向 @p path POST 原始字节（带 @p query 参数）
         *
         * 用于分片上传，请求体为原始文件数据（非 JSON）。
         * Content-Type 设置为 application/octet-stream。
         */
        virtual auto PostRaw(const QString& path, const QUrlQuery& query, const QByteArray& body, QObject* ctx, ApiReplyCallback cb) -> void;

        // ==================== GET ====================

        /**
         * @brief 使用共享 Bearer 令牌 GET @p path（无查询参数）
         */
        virtual auto Get(const QString& path, QObject* ctx, ApiReplyCallback cb) -> void;

        /**
         * @brief 使用共享 Bearer 令牌 GET @p path（带 @p query 参数）
         *
         * @param query 通过 QUrlQuery 构建的 URL 查询参数
         */
        virtual auto Get(const QString& path, const QUrlQuery& query, QObject* ctx, ApiReplyCallback cb) -> void;

        /**
         * @brief 使用调用者提供的 Bearer 令牌 GET @p path
         */
        virtual auto GetWithBearerToken(
            const QString& path,
            const QUrlQuery& query,
            const QString& bearerToken,
            QObject* ctx,
            ApiReplyCallback cb
        ) -> void;

        // ==================== PUT ====================

        /**
         * @brief 使用共享 Bearer 令牌向 @p path PUT JSON 请求体
         */
        virtual auto PutJson(const QString& path, const QJsonObject& body, QObject* ctx, ApiReplyCallback cb) -> void;

        /**
         * @brief 使用调用者提供的 Bearer 令牌 PUT JSON 请求体
         */
        virtual auto PutJsonWithBearerToken(
            const QString& path,
            const QJsonObject& body,
            const QString& bearerToken,
            QObject* ctx,
            ApiReplyCallback cb
        ) -> void;

        // ==================== PATCH ====================

        /**
         * @brief 使用共享 Bearer 令牌向 @p path PATCH JSON 请求体
         */
        virtual auto PatchJson(const QString& path, const QJsonObject& body, QObject* ctx, ApiReplyCallback cb) -> void;

        /**
         * @brief 使用调用者提供的 Bearer 令牌 PATCH JSON 请求体
         */
        virtual auto PatchJsonWithBearerToken(
            const QString& path,
            const QJsonObject& body,
            const QString& bearerToken,
            QObject* ctx,
            ApiReplyCallback cb
        ) -> void;

        // ==================== DELETE ====================

        /**
         * @brief 使用共享 Bearer 令牌 DELETE @p path（无请求体）
         */
        virtual auto Delete(const QString& path, QObject* ctx, ApiReplyCallback cb) -> void;

        /**
         * @brief 使用调用者提供的 Bearer 令牌 DELETE @p path（无请求体）
         */
        virtual auto DeleteWithBearerToken(
            const QString& path,
            const QString& bearerToken,
            QObject* ctx,
            ApiReplyCallback cb
        ) -> void;

        /**
         * @brief 通过 sendCustomRequest 发送带 JSON 请求体的 DELETE 请求
         *
         * QRestAccessManager::deleteResource() 不支持请求体。
         * 此方法使用 sendCustomRequest("DELETE", body) 绕过此限制。
         * 回收站批量删除端点需要此方法。
         */
        virtual auto DeleteJson(const QString& path, const QJsonObject& body, QObject* ctx, ApiReplyCallback cb) -> void;

        /**
         * @brief 使用调用者提供的 Bearer 令牌发送带 JSON 请求体的 DELETE 请求
         */
        virtual auto DeleteJsonWithBearerToken(
            const QString& path,
            const QJsonObject& body,
            const QString& bearerToken,
            QObject* ctx,
            ApiReplyCallback cb
        ) -> void;

        // ==================== Streaming ====================

        /**
         * @brief 获取原始 QNetworkAccessManager 用于流式下载
         *
         * @note DownloadEngine 使用此方法发起 GET 请求，其 QNetworkReply
         *       通过 readyRead 信号增量读取，而非完全缓存在内存中。
         */
        [[nodiscard]] auto NetworkAccessManager() -> QNetworkAccessManager*;

        /**
         * @brief 从工厂创建带有 Bearer 令牌的 QNetworkRequest
         *
         * @param path API 路径，例如 "/api/file/download/42"
         * @return 配置了基础 URL 和 Bearer 令牌的 QNetworkRequest
         */
        [[nodiscard]] auto CreateStreamingRequest(const QString& path) -> QNetworkRequest;

        ~ApiClient() override = default;

    private:
        QNetworkAccessManager m_nam;
        QRestAccessManager m_rest;
        QNetworkRequestFactory m_factory;
    };

} // namespace disk::qml::api
