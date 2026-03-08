/**
 * @file FolderService.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief High-level folder orchestration for the QML client
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <optional>

#include <dtos/FileDtos.hpp>

namespace disk::qml::api {
    class FolderApi;
}

namespace disk::qml::services {
    class TokenRefreshCoordinator;

    /**
     * @brief Folder service for the QML client.
     * @details Orchestrates folder-related business workflows:
     *   - Creating new folders with input validation
     *   - Fetching breadcrumb paths for navigation
     *   - Fetching folder tree structures for move/copy pickers
     *   - Delegating network requests to api::FolderApi
     *   - Mapping transport-level and envelope-level errors to user-friendly messages
     */
    class FolderService final {
    public:
        using CreateFolderCallback = std::function<void(std::optional<models::CreateFolderResultDto> result, QString errorMessage)>;
        using BreadcrumbCallback = std::function<void(std::optional<models::BreadcrumbResultDto> result, QString errorMessage)>;
        using FolderTreeCallback = std::function<void(std::optional<models::FolderTreeResultDto> result, QString errorMessage)>;

        explicit FolderService(api::FolderApi* folderApi, TokenRefreshCoordinator* coordinator = nullptr);

        /**
         * @brief Creates a new folder.
         * @details Validates the folder name locally before calling api::FolderApi::CreateFolder.
         *   On success the callback receives a populated CreateFolderResultDto.
         * @param name      Folder name (must not be empty or whitespace-only).
         * @param parentId  Parent folder ID (0 = root).
         * @param ctx       QObject lifetime guard; the callback is not invoked after ctx is destroyed.
         * @param cb        Receives (result, errorMessage). errorMessage is empty on success.
         */
        auto CreateFolder(const QString& name, qint64 parentId, QObject* ctx, CreateFolderCallback cb) -> void;

        /**
         * @brief Fetches the breadcrumb path from root to the given folder.
         * @param folderId  Folder ID to get breadcrumb for.
         * @param ctx       QObject lifetime guard.
         * @param cb        Receives (result, errorMessage). errorMessage is empty on success.
         */
        auto GetBreadcrumb(qint64 folderId, QObject* ctx, BreadcrumbCallback cb) -> void;

        /**
         * @brief Fetches the folder tree structure for move/copy destination pickers.
         * @param parentId  Root of the subtree to fetch (0 = root).
         * @param depth     Max depth (-1 = unlimited).
         * @param ctx       QObject lifetime guard.
         * @param cb        Receives (result, errorMessage). errorMessage is empty on success.
         */
        auto GetFolderTree(qint64 parentId, int depth, QObject* ctx, FolderTreeCallback cb) -> void;

    private:
        auto MapTransportError(const QString& networkError) const -> QString;
        auto MapEnvelopeError(const models::ApiEnvelope& envelope) const -> QString;

        api::FolderApi* m_folder_api;
        TokenRefreshCoordinator* m_coordinator{nullptr};
    };

} // namespace disk::qml::services
