/**
 * @file FileService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief High-level file orchestration for the QML client
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

#include <dtos/FileDtos.hpp>

namespace disk::qml::api {
    class FileApi;
}

namespace disk::qml::services {
    class TokenRefreshCoordinator;

    /**
     * @brief File service for the QML client.
     * @details Orchestrates file-related business workflows:
     *   - Listing files/folders with pagination and filters
     *   - Renaming files/folders with input validation
     *   - Moving, copying, and deleting files/folders in batch
     *   - Searching files with keyword and filters
     *   - Delegating network requests to api::FileApi
     *   - Mapping transport-level and envelope-level errors to user-friendly messages
     */
    class FileService final {
    public:
        using ListCallback = std::function<void(std::optional<models::FileListResultDto> result, QString errorMessage)>;
        using RenameCallback = std::function<void(std::optional<models::RenameResultDto> result, QString errorMessage)>;
        using MoveCallback = std::function<void(std::optional<models::MoveResultDto> result, QString errorMessage)>;
        using CopyCallback = std::function<void(std::optional<models::CopyResultDto> result, QString errorMessage)>;
        using DeleteCallback = std::function<void(std::optional<models::DeleteResultDto> result, QString errorMessage)>;
        using SearchCallback = std::function<void(std::optional<models::SearchResultDto> result, QString errorMessage)>;

        explicit FileService(api::FileApi* fileApi, TokenRefreshCoordinator* coordinator = nullptr);

        /**
         * @brief Lists files and folders in a given parent folder.
         * @param parentId   Parent folder ID (0 = root).
         * @param page       Page number (1-based).
         * @param pageSize   Items per page (1-100).
         * @param sortBy     Sort field (name|size|created_at|updated_at).
         * @param sortOrder  Sort direction (asc|desc).
         * @param type       Filter type (all|file|folder).
         * @param ctx        QObject lifetime guard.
         * @param cb         Receives (result, errorMessage). errorMessage is empty on success.
         */
        auto ListFiles(
            qint64 parentId,
            int page,
            int pageSize,
            const QString& sortBy,
            const QString& sortOrder,
            const QString& type,
            QObject* ctx,
            ListCallback cb
        ) -> void;

        /**
         * @brief Renames a file or folder.
         * @details Validates the new name locally before calling api::FileApi::Rename.
         * @param fileId   File/folder ID (positive integer).
         * @param newName  New name (must not be empty or whitespace-only).
         * @param ctx      QObject lifetime guard.
         * @param cb       Receives (result, errorMessage). errorMessage is empty on success.
         */
        auto RenameFile(qint64 fileId, const QString& newName, QObject* ctx, RenameCallback cb) -> void;

        /**
         * @brief Moves files/folders to a target folder.
         * @details Validates that fileIds is non-empty before calling api::FileApi::Move.
         * @param fileIds         List of file/folder IDs to move.
         * @param targetFolderId  Destination folder ID (0 = root).
         * @param ctx             QObject lifetime guard.
         * @param cb              Receives (result, errorMessage). errorMessage is empty on success.
         */
        auto MoveFiles(const QList<qint64>& fileIds, qint64 targetFolderId, QObject* ctx, MoveCallback cb) -> void;

        /**
         * @brief Copies files/folders to a target folder.
         * @details Validates that fileIds is non-empty before calling api::FileApi::Copy.
         * @param fileIds         List of file/folder IDs to copy.
         * @param targetFolderId  Destination folder ID (0 = root).
         * @param ctx             QObject lifetime guard.
         * @param cb              Receives (result, errorMessage). errorMessage is empty on success.
         */
        auto CopyFiles(const QList<qint64>& fileIds, qint64 targetFolderId, QObject* ctx, CopyCallback cb) -> void;

        /**
         * @brief Soft-deletes files/folders (moves to trash).
         * @details Validates that fileIds is non-empty before calling api::FileApi::Delete.
         * @param fileIds  List of file/folder IDs to delete.
         * @param ctx      QObject lifetime guard.
         * @param cb       Receives (result, errorMessage). errorMessage is empty on success.
         */
        auto DeleteFiles(const QList<qint64>& fileIds, QObject* ctx, DeleteCallback cb) -> void;

        /**
         * @brief Searches files by keyword.
         * @details Validates the keyword locally before calling api::FileApi::Search.
         * @param keyword   Search keyword (required, must not be empty).
         * @param type      Filter type (all|file|folder).
         * @param folderId  Scope to folder (-1 = global search).
         * @param page      Page number (1-based).
         * @param pageSize  Items per page (1-100).
         * @param ctx       QObject lifetime guard.
         * @param cb        Receives (result, errorMessage). errorMessage is empty on success.
         */
        auto SearchFiles(
            const QString& keyword,
            const QString& type,
            qint64 folderId,
            int page,
            int pageSize,
            QObject* ctx,
            SearchCallback cb
        ) -> void;

        /**
         * @brief Gets recent files (sorted by updated_at desc).
         * @details Calls ListFiles with sort_by=updated_at, sort_order=desc.
         * @param limit    Maximum number of files to return (1-100, default 10).
         * @param ctx      QObject lifetime guard.
         * @param cb       Receives (result, errorMessage). errorMessage is empty on success.
         */
        auto GetRecentFiles(
            int limit,
            QObject* ctx,
            ListCallback cb
        ) -> void;

    private:
        auto MapTransportError(const QString& networkError) const -> QString;
        auto MapEnvelopeError(const models::ApiEnvelope& envelope) const -> QString;

        api::FileApi* m_file_api;
        TokenRefreshCoordinator* m_coordinator{nullptr};
    };

} // namespace disk::qml::services
