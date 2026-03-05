/**
 * @file ShareApi.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Share API endpoints for create, list, detail, update, cancel, access, browse, download
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

    using ShareApiCallback = models::ApiCallback;

    class ApiClient;

    /**
     * @brief API wrapper for share-related endpoints.
     *
     * Owner endpoints (JWT auth via shared bearer token):
     * - POST   /api/share                      → Create
     * - GET    /api/share                      → List
     * - GET    /api/share/{share_id}           → GetDetail
     * - PUT    /api/share/{share_id}           → Update
     * - DELETE /api/share                      → Cancel
     *
     * Public endpoint (no auth):
     * - POST   /api/share/access/{share_id}    → Access
     *
     * Share-token endpoints (X-Share-Token header):
     * - GET    /api/share/browse/{share_id}    → Browse
     * - GET    /api/share/download/{share_id}/{file_id} → Download
     */
    class ShareApi {
    public:
        explicit ShareApi(ApiClient* client);

        // ==================== Owner endpoints (JWT) ====================

        /**
         * @brief POST /api/share — create a new share.
         *
         * @param fileIds      List of file IDs to share.
         * @param expireDays   Expiration in days (0 = permanent, default 7).
         * @param password     Optional access password (4-8 chars, empty = none).
         * @param permission   "view" or "download".
         * @param ctx          Context QObject for callback lifetime.
         * @param cb           Invoked with the server envelope on completion.
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
         * @brief GET /api/share — list shares owned by the current user.
         *
         * @param status    Filter: "all", "active", "expired", "cancelled".
         * @param page      Page number (1-based).
         * @param pageSize  Items per page (1-100).
         * @param ctx       Context QObject for callback lifetime.
         * @param cb        Invoked with the server envelope on completion.
         */
        virtual auto List(
            const QString& status,
            int page,
            int pageSize,
            QObject* ctx,
            ShareApiCallback cb
        ) -> void;

        /**
         * @brief GET /api/share/{share_id} — get share detail (owner view).
         *
         * @param shareId   Share ID string.
         * @param ctx       Context QObject for callback lifetime.
         * @param cb        Invoked with the server envelope on completion.
         */
        virtual auto GetDetail(const QString& shareId, QObject* ctx, ShareApiCallback cb) -> void;

        /**
         * @brief PUT /api/share/{share_id} — update share settings.
         *
         * @param shareId      Share ID string.
         * @param expireDays   New expiration in days (-1 = no change, 0 = permanent).
         * @param password     New password (empty = remove password, null = no change).
         * @param permission   New permission ("view"/"download", empty = no change).
         * @param ctx          Context QObject for callback lifetime.
         * @param cb           Invoked with the server envelope on completion.
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
         * @brief DELETE /api/share — batch cancel shares.
         *
         * @param shareIds  List of share ID strings to cancel.
         * @param ctx       Context QObject for callback lifetime.
         * @param cb        Invoked with the server envelope on completion.
         */
        virtual auto Cancel(
            const QStringList& shareIds,
            QObject* ctx,
            ShareApiCallback cb
        ) -> void;

        // ==================== Public endpoint (no auth) ====================

        /**
         * @brief POST /api/share/access/{share_id} — verify share access (get share token).
         *
         * @param shareId   Share ID string.
         * @param password  Optional access password (empty if no password required).
         * @param ctx       Context QObject for callback lifetime.
         * @param cb        Invoked with the server envelope on completion.
         */
        virtual auto Access(
            const QString& shareId,
            const QString& password,
            QObject* ctx,
            ShareApiCallback cb
        ) -> void;

        // ==================== Share-token endpoints (X-Share-Token) ====================

        /**
         * @brief GET /api/share/browse/{share_id} — browse share contents.
         *
         * @param shareId     Share ID string.
         * @param shareToken  Token from Access() response, sent as X-Share-Token header.
         * @param folderId    Optional folder ID for sub-navigation (-1 = root).
         * @param ctx         Context QObject for callback lifetime.
         * @param cb          Invoked with the server envelope on completion.
         */
        virtual auto Browse(
            const QString& shareId,
            const QString& shareToken,
            qint64 folderId,
            QObject* ctx,
            ShareApiCallback cb
        ) -> void;

        /**
         * @brief GET /api/share/download/{share_id}/{file_id} — download a shared file.
         *
         * @param shareId     Share ID string.
         * @param fileId      File ID to download.
         * @param shareToken  Token from Access() response, sent as X-Share-Token header.
         * @param ctx         Context QObject for callback lifetime.
         * @param cb          Invoked with raw reply (binary body).
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
