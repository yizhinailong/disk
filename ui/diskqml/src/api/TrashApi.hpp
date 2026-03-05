/**
 * @file TrashApi.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Trash API endpoints for list, restore, delete, and clear all
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
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
     * @brief API wrapper for trash-related endpoints (all require JWT auth).
     *
     * @details
     * Wrapped endpoints:
     * - GET    /api/trash          -> TrashListResponse (paginated)
     * - POST   /api/trash/restore  -> TrashBatchResponse (restore items)
     * - DELETE  /api/trash          -> TrashBatchResponse (permanently delete, uses DeleteJson with body)
     * - DELETE  /api/trash/all      -> TrashClearResponse (empty trash, no body)
     */
    class TrashApi {
    public:
        /**
         * @brief Construct a TrashApi bound to the given API client.
         *
         * @param client  Pointer to the shared ApiClient. The caller is responsible for
         *                ensuring @p client outlives this TrashApi instance.
         */
        explicit TrashApi(ApiClient* client);

        /**
         * @brief GET /api/trash - fetch paginated trash list.
         *
         * @param page      Page number (1-based).
         * @param pageSize  Items per page (1-100).
         * @param ctx       Context QObject; callback suppressed after destruction.
         * @param cb        Invoked with the server envelope on completion.
         */
        virtual auto List(
            int page,
            int pageSize,
            QObject* ctx,
            TrashApiCallback cb
        ) -> void;

        /**
         * @brief POST /api/trash/restore - restore trashed items.
         *
         * @param trashIds  List of trash item IDs to restore.
         * @param ctx       Context QObject; callback suppressed after destruction.
         * @param cb        Invoked with the server envelope on completion.
         */
        virtual auto Restore(
            const QList<qint64>& trashIds,
            QObject* ctx,
            TrashApiCallback cb
        ) -> void;

        /**
         * @brief DELETE /api/trash - permanently delete selected trash items.
         *
         * @note Uses ApiClient::DeleteJson because the request has a JSON body with trash_ids.
         *
         * @param trashIds  List of trash item IDs to permanently delete.
         * @param ctx       Context QObject; callback suppressed after destruction.
         * @param cb        Invoked with the server envelope on completion.
         */
        virtual auto Delete(
            const QList<qint64>& trashIds,
            QObject* ctx,
            TrashApiCallback cb
        ) -> void;

        /**
         * @brief DELETE /api/trash/all - empty trash (delete all).
         *
         * @note Uses ApiClient::Delete (no body).
         *
         * @param ctx  Context QObject; callback suppressed after destruction.
         * @param cb   Invoked with the server envelope on completion.
         */
        virtual auto ClearAll(QObject* ctx, TrashApiCallback cb) -> void;

        virtual ~TrashApi() = default;

    private:
        ApiClient* m_client;
    };

} // namespace disk::qml::api
