/**
 * @file TrashApi.hpp
 * @brief 回收站服务 API 客户端
 * @details 提供回收站项目列表、恢复、彻底删除、清空等回收站相关的 HTTP API 调用
 * @author LiuFeng (liufeng.code@outlook.com)
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 */
#pragma once

#include <QList>
#include <QObject>

#include <api/ApiClient.hpp>
#include <dtos/ApiEnvelope.hpp>

namespace disk::qml::api {

    using TrashApiCallback = models::ApiCallback;

    class ApiClient;

    /**
     * @brief 回收站相关 API 封装（均需 JWT 认证）
     *
     * @details
     * 封装的端点：
     * - GET    /api/trash          -> TrashListResponse (分页)
     * - POST   /api/trash/restore  -> TrashBatchResponse (恢复项目)
     * - DELETE  /api/trash          -> TrashBatchResponse (彻底删除，使用 DeleteJson 携带请求体)
     * - DELETE  /api/trash/all      -> TrashClearResponse (清空回收站，无请求体)
     */
    class TrashApi {
    public:
        /**
         * @brief 构造回收站 API 客户端
         *
         * @param client API 客户端指针，调用者需确保该指针的生命周期长于此实例
         */
        explicit TrashApi(ApiClient* client);

        /**
         * @brief GET /api/trash - 获取分页回收站列表
         *
         * @param page 页码（从 1 开始）
         * @param pageSize 每页条数（1-100）
         * @param ctx 上下文 QObject，销毁时取消回调
         * @param cb 服务器响应回调
         */
        virtual auto List(
            int page,
            int pageSize,
            QObject* ctx,
            TrashApiCallback cb
        ) -> void;

        /**
         * @brief POST /api/trash/restore - 恢复回收站项目
         *
         * @param trashIds 要恢复的回收站项目 ID 列表
         * @param ctx 上下文 QObject，销毁时取消回调
         * @param cb 服务器响应回调
         */
        virtual auto Restore(
            const QList<qint64>& trashIds,
            QObject* ctx,
            TrashApiCallback cb
        ) -> void;

        /**
         * @brief DELETE /api/trash - 彻底删除选中的回收站项目
         *
         * @note 使用 ApiClient::DeleteJson 因为请求需要携带 JSON 请求体（包含 trash_ids）。
         *
         * @param trashIds 要彻底删除的回收站项目 ID 列表
         * @param ctx 上下文 QObject，销毁时取消回调
         * @param cb 服务器响应回调
         */
        virtual auto Delete(
            const QList<qint64>& trashIds,
            QObject* ctx,
            TrashApiCallback cb
        ) -> void;

        /**
         * @brief DELETE /api/trash/all - 清空回收站（删除所有）
         *
         * @note 使用 ApiClient::Delete（无请求体）。
         *
         * @param ctx 上下文 QObject，销毁时取消回调
         * @param cb 服务器响应回调
         */
        virtual auto ClearAll(QObject* ctx, TrashApiCallback cb) -> void;

        virtual ~TrashApi() = default;

    private:
        ApiClient* m_client;
    };

} // namespace disk::qml::api
