/**
 * @file ShareService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief High-level share orchestration for the QML client
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>

#include <dtos/ShareDtos.hpp>

namespace disk::qml::api {
    class ShareApi;
}

namespace disk::qml::services {

    /**
     * @brief Share service for the QML client.
     * @details Orchestrates share-related business workflows:
     *   - Creating shares
     *   - Listing shares with status filter and pagination
     *   - Canceling shares in batch
     *   - Delegating network requests to api::ShareApi
     *   - Mapping transport-level and envelope-level errors to user-friendly messages
     */
    class ShareService final {
    public:
        using CreateCallback = std::function<void(std::optional<models::CreateShareResultDto> result, QString errorMessage)>;
        using ListCallback = std::function<void(std::optional<models::ShareListResultDto> result, QString errorMessage)>;
        using CancelCallback = std::function<void(std::optional<models::CancelShareResultDto> result, QString errorMessage)>;

        explicit ShareService(api::ShareApi* shareApi);

        /**
         * @brief Creates a new share.
         * @param fileIds      List of file IDs to share.
         * @param expireDays   Expiration in days (0 = permanent, default 7).
         * @param password     Optional access password (4-8 chars, empty = none).
         * @param permission   "view" or "download".
         * @param ctx          QObject lifetime guard.
         * @param cb           Receives (result, errorMessage). errorMessage is empty on success.
         */
        auto CreateShare(
            const QList<qint64>& fileIds,
            int expireDays,
            const QString& password,
            const QString& permission,
            QObject* ctx,
            CreateCallback cb
        ) -> void;

        /**
         * @brief Lists shares with status filter and pagination.
         * @param status    Filter: "all", "active", "expired", "cancelled".
         * @param page      Page number (1-based).
         * @param pageSize  Items per page (1-100).
         * @param ctx       QObject lifetime guard.
         * @param cb        Receives (result, errorMessage). errorMessage is empty on success.
         */
        auto ListShares(
            const QString& status,
            int page,
            int pageSize,
            QObject* ctx,
            ListCallback cb
        ) -> void;

        /**
         * @brief Cancels shares in batch.
         * @details Validates that shareIds is non-empty before calling api::ShareApi::Cancel.
         * @param shareIds  List of share ID strings to cancel.
         * @param ctx       QObject lifetime guard.
         * @param cb        Receives (result, errorMessage). errorMessage is empty on success.
         */
        auto CancelShares(
            const QStringList& shareIds,
            QObject* ctx,
            CancelCallback cb
        ) -> void;

    private:
        auto MapTransportError(const QString& networkError) const -> QString;
        auto MapEnvelopeError(const models::ApiEnvelope& envelope) const -> QString;

        api::ShareApi* m_share_api;
    };

} // namespace disk::qml::services
