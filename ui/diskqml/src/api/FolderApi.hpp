/**
 * @file FolderApi.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Folder API endpoints for create, tree, and breadcrumb
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <QObject>
#include <QString>

#include <dtos/ApiEnvelope.hpp>

namespace disk::qml::api {

    using FolderApiCallback = models::ApiCallback;

    class ApiClient;

    /**
     * @brief API wrapper for folder-related endpoints (all require JWT auth).
     *
     * @details
     * All methods use the shared bearer token configured on ApiClient.
     * The caller must ensure the token is set (via ApiClient::SetBearerToken)
     * before invoking these methods.
     *
     * Wrapped endpoints:
     * - POST /api/folder/create                   -> CreateFolderResponse
     * - GET  /api/folder/tree                      -> FolderTreeNode[]
     * - GET  /api/folder/{folder_id}/breadcrumb    -> BreadcrumbResponse
     */
    class FolderApi {
    public:
        /**
         * @brief Construct a FolderApi bound to the given API client.
         *
         * @param client  Pointer to the shared ApiClient. The caller is responsible for
         *                ensuring @p client outlives this FolderApi instance.
         */
        explicit FolderApi(ApiClient* client);

        /**
         * @brief POST /api/folder/create - create a new folder.
         *
         * @details
         * Request body: { "name": "<string>", "parent_id": <uint64> }
         * Response data shape: { id, name, parent_id, path, created_at }
         *
         * @param name      Folder name (1-255 ASCII printable chars).
         * @param parentId  Parent folder ID (0 = root).
         * @param ctx       Context QObject; callback suppressed after destruction.
         * @param cb        Invoked with the server envelope on completion.
         */
        virtual auto CreateFolder(
            const QString& name,
            qint64 parentId,
            QObject* ctx,
            FolderApiCallback cb
        ) -> void;

        /**
         * @brief GET /api/folder/tree - fetch folder tree structure.
         *
         * @details
         * Query parameters:
         * - parent_id  (default 0, root)
         * - depth      (default -1, unlimited depth)
         *
         * Response data shape: recursive { id, name, children: [...] }
         *
         * @param parentId  Root of the subtree to fetch (0 = root).
         * @param depth     Max depth (-1 = unlimited).
         * @param ctx       Context QObject; callback suppressed after destruction.
         * @param cb        Invoked with the server envelope on completion.
         */
        virtual auto GetTree(
            qint64 parentId,
            int depth,
            QObject* ctx,
            FolderApiCallback cb
        ) -> void;

        /**
         * @brief GET /api/folder/{folder_id}/breadcrumb - fetch breadcrumb path.
         *
         * @details
         * Response data shape: { "path": [ { id, name }, ... ] }
         *
         * @param folderId  Folder ID to get breadcrumb for.
         * @param ctx       Context QObject; callback suppressed after destruction.
         * @param cb        Invoked with the server envelope on completion.
         */
        virtual auto GetBreadcrumb(qint64 folderId, QObject* ctx, FolderApiCallback cb) -> void;

        virtual ~FolderApi() = default;

    private:
        ApiClient* m_client;
    };

} // namespace disk::qml::api
