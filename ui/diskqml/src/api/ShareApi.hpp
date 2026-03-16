/**
 * @file ShareApi.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 分享 API 端点：创建、列表、详情、更新、取消、访问、浏览、下载
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <api/ApiClient.hpp>
#include <dtos/ApiEnvelope.hpp>

namespace disk::qml::api {

    using ShareApiCallback = models::ApiCallback;

    class ApiClient;

    /**
     * @brief 分享相关 API 封装
     *
     * 所有者端点（通过共享 Bearer 令牌进行 JWT 认证）：
     * - POST   /api/share                      → Create
     * - GET    /api/share                      → List
     * - GET    /api/share/{share_id}           → GetDetail
     * - PUT    /api/share/{share_id}           → Update
     * - DELETE /api/share                      → Cancel
     *
     * 公开端点（无需认证）：
     * - POST   /api/share/access/{share_id}    → Access
     *
     * 分享令牌端点（X-Share-Token 请求头）：
     * - GET    /api/share/browse/{share_id}    → Browse
     * - GET    /api/share/download/{share_id}/{file_id} → Download
     */
    class ShareApi {
    public:
        explicit ShareApi(ApiClient* client);

        // ==================== 所有者端点（JWT 认证） ====================

        /**
         * @brief POST /api/share — 创建新分享。
         *
         * @param fileIds      要分享的文件 ID 列表。
         * @param expireDays   有效期天数（0 = 永久，默认 7 天）。
         * @param password     可选访问密码（4-8 个字符，空表示无密码）。
         * @param permission   "view" 或 "download"。
         * @param ctx          上下文 QObject，用于回调生命周期管理。
         * @param cb           完成时调用，返回服务器响应封装。
         */
        virtual auto Create(
            const QList<qint64>& fileIds,
            int expireDays,
            const QString& password,
            const QString& permission,
            QObject* ctx,
            ShareApiCallback cb
        ) -> void;

        /**
         * @brief GET /api/share — 列出当前用户拥有的分享。
         *
         * @param status    过滤条件："all"、"active"、"expired"、"cancelled"。
         * @param page      页码（从 1 开始）。
         * @param pageSize  每页条数（1-100）。
         * @param ctx       上下文 QObject，用于回调生命周期管理。
         * @param cb        完成时调用，返回服务器响应封装。
         */
        virtual auto List(
            const QString& status,
            int page,
            int pageSize,
            QObject* ctx,
            ShareApiCallback cb
        ) -> void;

        /**
         * @brief GET /api/share/{share_id} — 获取分享详情（所有者视图）。
         *
         * @param shareId   分享 ID 字符串。
         * @param ctx       上下文 QObject，用于回调生命周期管理。
         * @param cb        完成时调用，返回服务器响应封装。
         */
        virtual auto GetDetail(const QString& shareId, QObject* ctx, ShareApiCallback cb) -> void;

        /**
         * @brief PUT /api/share/{share_id} — 更新分享设置。
         *
         * @param shareId      分享 ID 字符串。
         * @param expireDays   新的有效期天数（-1 = 不变，0 = 永久）。
         * @param password     新密码（空 = 移除密码，null = 不变）。
         * @param permission   新权限（"view"/"download"，空 = 不变）。
         * @param ctx          上下文 QObject，用于回调生命周期管理。
         * @param cb           完成时调用，返回服务器响应封装。
         */
        virtual auto Update(
            const QString& shareId,
            int expireDays,
            const std::optional<QString>& password,
            const QString& permission,
            QObject* ctx,
            ShareApiCallback cb
        ) -> void;

        /**
         * @brief DELETE /api/share — 批量取消分享。
         *
         * @param shareIds  要取消的分享 ID 字符串列表。
         * @param ctx       上下文 QObject，用于回调生命周期管理。
         * @param cb        完成时调用，返回服务器响应封装。
         */
        virtual auto Cancel(
            const QStringList& shareIds,
            QObject* ctx,
            ShareApiCallback cb
        ) -> void;

        // ==================== 公开端点（无需认证） ====================

        /**
         * @brief POST /api/share/access/{share_id} — 验证分享访问（获取分享令牌）。
         *
         * @param shareId   分享 ID 字符串。
         * @param password  可选访问密码（无密码时为空）。
         * @param ctx       上下文 QObject，用于回调生命周期管理。
         * @param cb        完成时调用，返回服务器响应封装。
         */
        virtual auto Access(
            const QString& shareId,
            const QString& password,
            QObject* ctx,
            ShareApiCallback cb
        ) -> void;

        // ==================== 分享令牌端点（X-Share-Token） ====================

        /**
         * @brief GET /api/share/browse/{share_id} — 浏览分享内容。
         *
         * @param shareId     分享 ID 字符串。
         * @param shareToken  Access() 响应返回的令牌，作为 X-Share-Token 请求头发送。
         * @param folderId    可选文件夹 ID，用于子目录导航（-1 = 根目录）。
         * @param ctx         上下文 QObject，用于回调生命周期管理。
         * @param cb          完成时调用，返回服务器响应封装。
         */
        virtual auto Browse(
            const QString& shareId,
            const QString& shareToken,
            qint64 folderId,
            QObject* ctx,
            ShareApiCallback cb
        ) -> void;

        /**
         * @brief GET /api/share/download/{share_id}/{file_id} — 下载分享文件。
         *
         * @param shareId     分享 ID 字符串。
         * @param fileId      要下载的文件 ID。
         * @param shareToken  Access() 响应返回的令牌，作为 X-Share-Token 请求头发送。
         * @param ctx         上下文 QObject，用于回调生命周期管理。
         * @param cb          完成时调用，返回原始响应（二进制数据）。
         */
        virtual auto Download(
            const QString& shareId,
            qint64 fileId,
            const QString& shareToken,
            QObject* ctx,
            ApiReplyCallback cb
        ) -> void;

        virtual ~ShareApi() = default;

    private:
        ApiClient* m_client;
    };

} // namespace disk::qml::api
