/**
 * @file FileApi.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief File API endpoints for list, detail, download, rename, move, copy, delete, search
 * @version 0.1
 * @date 2026-03-05
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

    using FileApiCallback = models::ApiCallback;

    class ApiClient;

    /**
     * @brief API wrapper for file-related endpoints (all require JWT auth).
     *
     * @details
     * All methods use the shared bearer token configured on ApiClient.
     * The caller must ensure the token is set (via ApiClient::SetBearerToken)
     * before invoking these methods.
     *
     * Wrapped endpoints:
     * - GET    /api/file/list                      -> FileListResponse
     * - GET    /api/file/{file_id}                  -> FileDetailResponse
     * - GET    /api/file/download/{file_id}/info    -> DownloadInfoResponse
     * - GET    /api/file/download/{file_id}         -> binary (download engine)
     * - PUT    /api/file/{file_id}/rename           -> RenameResponse
     * - PUT    /api/file/move                       -> MoveResponse
     * - POST   /api/file/copy                       -> CopyResponse
     * - DELETE  /api/file                            -> DeleteResponse
     * - GET    /api/file/search                     -> SearchResponse
     */
    class FileApi {
    public:
        /**
         * @brief Construct a FileApi bound to the given API client.
         *
         * @param client  Pointer to the shared ApiClient. The caller is responsible for
         *                ensuring @p client outlives this FileApi instance.
         */
        explicit FileApi(ApiClient* client);

        /**
         * @brief GET /api/file/list - fetch file list with pagination and filters.
         *
         * @details
         * Query parameters:
         * - parent_id   (default 0, root folder)
         * - page        (default 1)
         * - page_size   (default 20, max 100)
         * - sort_by     (name|size|created_at|updated_at, default name)
         * - sort_order  (asc|desc, default asc)
         * - type        (all|file|folder, default all)
         *
         * @param parentId   Parent folder ID (0 = root).
         * @param page       Page number (1-based).
         * @param pageSize   Items per page (1-100).
         * @param sortBy     Sort field.
         * @param sortOrder  Sort direction.
         * @param type       Filter type.
         * @param ctx        Context QObject; callback suppressed after destruction.
         * @param cb         Invoked with the server envelope on completion.
         */
        virtual auto List(
            qint64 parentId,
            int page,
            int pageSize,
            const QString& sortBy,
            const QString& sortOrder,
            const QString& type,
            QObject* ctx,
            FileApiCallback cb
        ) -> void;

        /**
         * @brief GET /api/file/{file_id} - fetch file detail.
         *
         * @param fileId  File ID (positive integer).
         * @param ctx     Context QObject; callback suppressed after destruction.
         * @param cb      Invoked with the server envelope on completion.
         */
        virtual auto GetDetail(qint64 fileId, QObject* ctx, FileApiCallback cb) -> void;

        /**
         * @brief GET /api/file/download/{file_id}/info - fetch download metadata.
         *
         * @details
         * Response data shape: { file_id, filename, file_size, file_hash,
         *                        mime_type, supports_range }
         *
         * @param fileId  File ID (positive integer).
         * @param ctx     Context QObject; callback suppressed after destruction.
         * @param cb      Invoked with the server envelope on completion.
         */
        virtual auto DownloadInfo(qint64 fileId, QObject* ctx, FileApiCallback cb) -> void;

        /**
         * @brief GET /api/file/download/{file_id} - download file binary.
         *
         * @note This returns raw binary data. The callback receives the raw byte body
         *       rather than a JSON envelope. Used by the download/transfer engine.
         *
         * @param fileId  File ID (positive integer).
         * @param ctx     Context QObject; callback suppressed after destruction.
         * @param cb      Invoked with the raw reply (code/body).
         */
        virtual auto Download(qint64 fileId, QObject* ctx, ApiReplyCallback cb) -> void;

        /**
         * @brief PUT /api/file/{file_id}/rename - rename a file.
         *
         * @param fileId   File ID (positive integer).
         * @param newName  New file name.
         * @param ctx      Context QObject; callback suppressed after destruction.
         * @param cb       Invoked with the server envelope on completion.
         */
        virtual auto Rename(
            qint64 fileId,
            const QString& newName,
            QObject* ctx,
            FileApiCallback cb
        ) -> void;

        /**
         * @brief PUT /api/file/move - move files to a target folder.
         *
         * @param fileIds         List of file IDs to move.
         * @param targetFolderId  Destination folder ID (0 = root).
         * @param ctx             Context QObject; callback suppressed after destruction.
         * @param cb              Invoked with the server envelope on completion.
         */
        virtual auto Move(
            const QList<qint64>& fileIds,
            qint64 targetFolderId,
            QObject* ctx,
            FileApiCallback cb
        ) -> void;

        /**
         * @brief POST /api/file/copy - copy files to a target folder.
         *
         * @param fileIds         List of file IDs to copy.
         * @param targetFolderId  Destination folder ID (0 = root).
         * @param ctx             Context QObject; callback suppressed after destruction.
         * @param cb              Invoked with the server envelope on completion.
         */
        virtual auto Copy(
            const QList<qint64>& fileIds,
            qint64 targetFolderId,
            QObject* ctx,
            FileApiCallback cb
        ) -> void;

        /**
         * @brief DELETE /api/file - soft-delete files (move to trash).
         *
         * @param fileIds  List of file IDs to delete.
         * @param ctx      Context QObject; callback suppressed after destruction.
         * @param cb       Invoked with the server envelope on completion.
         */
        virtual auto Delete(
            const QList<qint64>& fileIds,
            QObject* ctx,
            FileApiCallback cb
        ) -> void;

        /**
         * @brief GET /api/file/search - search files by keyword.
         *
         * @details
         * Query parameters:
         * - keyword    (required, 1-100 chars)
         * - type       (all|file|folder, default all)
         * - folder_id  (optional, scoped search)
         * - page       (default 1)
         * - page_size  (default 20, max 100)
         *
         * @param keyword   Search keyword (required).
         * @param type      Filter type.
         * @param folderId  Scope to folder (-1 = global search).
         * @param page      Page number (1-based).
         * @param pageSize  Items per page (1-100).
         * @param ctx       Context QObject; callback suppressed after destruction.
         * @param cb        Invoked with the server envelope on completion.
         */
        virtual auto Search(
            const QString& keyword,
            const QString& type,
            qint64 folderId,
            int page,
            int pageSize,
            QObject* ctx,
            FileApiCallback cb
        ) -> void;

        virtual ~FileApi() = default;

    private:
        ApiClient* m_client;
    };

} // namespace disk::qml::api
