/**
 * @file TrashService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief High-level trash orchestration for the QML client
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
#include <functional>
#include <optional>

#include <dtos/TrashDtos.hpp>

namespace disk::qml::api {
    class TrashApi;
}

namespace disk::qml::services {

    /**
     * @brief Trash service for the QML client.
     * @details Orchestrates trash-related business workflows:
     *   - Listing trashed items with pagination
     *   - Restoring trashed items in batch
     *   - Permanently deleting trashed items in batch
     *   - Clearing all trash
     *   - Delegating network requests to api::TrashApi
     *   - Mapping transport-level and envelope-level errors to user-friendly messages
     */
    class TrashService final {
    public:
        using ListCallback = std::function<void(std::optional<models::TrashListResultDto> result, QString errorMessage)>;
        using RestoreCallback = std::function<void(std::optional<models::TrashBatchResultDto> result, QString errorMessage)>;
        using DeleteCallback = std::function<void(std::optional<models::TrashBatchResultDto> result, QString errorMessage)>;
        using ClearAllCallback = std::function<void(std::optional<models::TrashClearResultDto> result, QString errorMessage)>;

        explicit TrashService(api::TrashApi* trashApi);

        /**
         * @brief Lists trashed items with pagination.
         * @param page      Page number (1-based).
         * @param pageSize  Items per page (1-100).
         * @param ctx       QObject lifetime guard.
         * @param cb        Receives (result, errorMessage). errorMessage is empty on success.
         */
        auto ListTrash(
            int page,
            int pageSize,
            QObject* ctx,
            ListCallback cb
        ) -> void;

        /**
         * @brief Restores trashed items.
         * @details Validates that trashIds is non-empty before calling api::TrashApi::Restore.
         * @param trashIds  List of trash item IDs to restore.
         * @param ctx       QObject lifetime guard.
         * @param cb        Receives (result, errorMessage). errorMessage is empty on success.
         */
        auto RestoreItems(const QList<qint64>& trashIds, QObject* ctx, RestoreCallback cb) -> void;

        /**
         * @brief Permanently deletes trashed items.
         * @details Validates that trashIds is non-empty before calling api::TrashApi::Delete.
         * @param trashIds  List of trash item IDs to permanently delete.
         * @param ctx       QObject lifetime guard.
         * @param cb        Receives (result, errorMessage). errorMessage is empty on success.
         */
        auto DeleteItems(const QList<qint64>& trashIds, QObject* ctx, DeleteCallback cb) -> void;

        /**
         * @brief Clears all trash (empties the trash).
         * @param ctx  QObject lifetime guard.
         * @param cb   Receives (result, errorMessage). errorMessage is empty on success.
         */
        auto ClearAll(QObject* ctx, ClearAllCallback cb) -> void;

    private:
        auto MapTransportError(const QString& networkError) const -> QString;
        auto MapEnvelopeError(const models::ApiEnvelope& envelope) const -> QString;

        api::TrashApi* m_trash_api;
    };

} // namespace disk::qml::services
