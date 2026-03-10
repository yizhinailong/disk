/**
 * @file FileDtos.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief File & folder response DTOs and JSON parsing helpers for the QML client
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVector>
#include <optional>

#include <dtos/ApiEnvelope.hpp>

namespace disk::qml::models {

    // ==================== Shared Structs ====================

    /**
     * @brief Pagination metadata from list responses.
     *
     * @details
     * Mirrors backend Pagination: { "page", "page_size", "total", "total_pages" }
     */
    struct PaginationDto {
        int page{};
        int pageSize{};
        int total{};
        int totalPages{};
    };

    // ==================== File List ====================

    /**
     * @brief A single item (file or folder) in a file list.
     *
     * @details
     * Mirrors backend FileListItem. type is "file" or "folder".
     * For files: size, mimeType, hash are populated.
     * For folders: itemCount is populated.
     */
    struct FileListItemDto {
        quint64 id{};
        QString name;
        QString type; ///< "file" or "folder"
        // File fields:
        quint64 size{};
        QString mimeType;
        QString hash;
        // Folder fields:
        int itemCount{};
        // Common:
        QString createdAt;
        QString updatedAt;
    };

    /**
     * @brief Result of a file list query.
     */
    struct FileListResultDto {
        QVector<FileListItemDto> items;
        PaginationDto pagination;
    };

    // ==================== Breadcrumb ====================

    /**
     * @brief A single breadcrumb entry.
     */
    struct BreadcrumbItemDto {
        quint64 id{};
        QString name;
    };

    /**
     * @brief Breadcrumb path from root to current folder.
     */
    struct BreadcrumbResultDto {
        QVector<BreadcrumbItemDto> path;
    };

    // ==================== Folder Tree ====================

    /**
     * @brief A node in the folder tree.
     */
    struct FolderTreeNodeDto {
        quint64 id{};
        QString name;
        QVector<FolderTreeNodeDto> children;
    };

    /**
     * @brief Result of a folder tree query.
     */
    struct FolderTreeResultDto {
        QVector<FolderTreeNodeDto> tree;
    };

    // ==================== Create Folder ====================

    /**
     * @brief Result of creating a folder.
     */
    struct CreateFolderResultDto {
        quint64 id{};
        QString name;
        quint64 parentId{};
        QString path;
        QString createdAt;
    };

    // ==================== Rename ====================

    /**
     * @brief Result of renaming a file or folder.
     */
    struct RenameResultDto {
        quint64 id{};
        QString name;
        QString updatedAt;
    };

    // ==================== Move ====================

    /**
     * @brief Result of a move operation.
     */
    struct MoveResultDto {
        int movedCount{};
    };

    // ==================== Copy ====================

    /**
     * @brief ID mapping for a copied file.
     */
    struct FileIdMappingDto {
        quint64 oldId{};
        quint64 newId{};
    };

    /**
     * @brief Result of a copy operation.
     */
    struct CopyResultDto {
        int copiedCount{};
        QVector<FileIdMappingDto> newFiles;
    };

    // ==================== Delete ====================

    /**
     * @brief Result of a delete operation (soft-delete to trash).
     */
    struct DeleteResultDto {
        int deletedCount{};
    };

    // ==================== Search ====================

    /**
     * @brief A single search result item.
     *
     * @details
     * Extends FileListItemDto with a path field for display.
     */
    struct SearchResultItemDto {
        quint64 id{};
        QString name;
        QString type;
        quint64 size{};
        QString mimeType;
        QString hash;
        int itemCount{};
        QString createdAt;
        QString updatedAt;
        QString path; ///< Breadcrumb path string
    };

    /**
     * @brief Result of a file search query.
     */
    struct SearchResultDto {
        QVector<SearchResultItemDto> items;
        PaginationDto pagination;
    };

    // ==================== JSON Parsing Helpers ====================

    /**
     * @brief Parse pagination from a JSON object.
     */
    inline auto ParsePagination(const QJsonObject& obj) -> PaginationDto {
        PaginationDto p;
        p.page = obj.value(QLatin1String("page")).toInt();
        p.pageSize = obj.value(QLatin1String("page_size")).toInt();
        p.total = obj.value(QLatin1String("total")).toInt();
        p.totalPages = obj.value(QLatin1String("total_pages")).toInt();
        return p;
    }

    /**
     * @brief Parse a single FileListItem from a JSON object.
     */
    inline auto ParseFileListItem(const QJsonObject& obj) -> FileListItemDto {
        FileListItemDto item;
        item.id = static_cast<quint64>(obj.value(QLatin1String("id")).toDouble());
        item.name = obj.value(QLatin1String("name")).toString();
        item.type = obj.value(QLatin1String("type")).toString();
        item.size = static_cast<quint64>(obj.value(QLatin1String("size")).toDouble());
        item.mimeType = obj.value(QLatin1String("mime_type")).toString();
        item.hash = obj.value(QLatin1String("hash")).toString();
        item.itemCount = obj.value(QLatin1String("item_count")).toInt();
        item.createdAt = obj.value(QLatin1String("created_at")).toString();
        item.updatedAt = obj.value(QLatin1String("updated_at")).toString();
        return item;
    }

    /**
     * @brief Parse a file list result from envelope data.
     *
     * @details
     * Expected shape: data = { "items": [...], "pagination": { ... } }
     */
    inline auto ParseFileListResult(const QJsonValue& dataVal) -> std::optional<FileListResultDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }

        const QJsonObject obj = dataVal.toObject();
        const QJsonValue itemsVal = obj.value(QLatin1String("items"));
        if (!itemsVal.isArray()) {
            return std::nullopt;
        }

        FileListResultDto result;

        const QJsonArray itemsArr = itemsVal.toArray();
        result.items.reserve(itemsArr.size());
        for (const auto& val : itemsArr) {
            if (val.isObject()) {
                result.items.append(ParseFileListItem(val.toObject()));
            }
        }

        const QJsonValue pagVal = obj.value(QLatin1String("pagination"));
        if (pagVal.isObject()) {
            result.pagination = ParsePagination(pagVal.toObject());
        }

        return result;
    }

    /**
     * @brief Parse breadcrumb result from envelope data.
     *
     * @details
     * Expected shape: data = { "path": [ { "id": N, "name": "..." }, ... ] }
     */
    inline auto ParseBreadcrumbResult(const QJsonValue& dataVal) -> std::optional<BreadcrumbResultDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }

        const QJsonObject obj = dataVal.toObject();
        const QJsonValue pathVal = obj.value(QLatin1String("path"));
        if (!pathVal.isArray()) {
            return std::nullopt;
        }

        BreadcrumbResultDto result;
        const QJsonArray pathArr = pathVal.toArray();
        result.path.reserve(pathArr.size());
        for (const auto& val : pathArr) {
            if (val.isObject()) {
                const QJsonObject itemObj = val.toObject();
                BreadcrumbItemDto item;
                item.id = static_cast<quint64>(itemObj.value(QLatin1String("id")).toDouble());
                item.name = itemObj.value(QLatin1String("name")).toString();
                result.path.append(item);
            }
        }

        return result;
    }

    /**
     * @brief Parse a single folder tree node recursively.
     */
    inline auto ParseFolderTreeNode(const QJsonObject& obj) -> FolderTreeNodeDto {
        FolderTreeNodeDto node;
        node.id = static_cast<quint64>(obj.value(QLatin1String("id")).toDouble());
        node.name = obj.value(QLatin1String("name")).toString();

        const QJsonValue childrenVal = obj.value(QLatin1String("children"));
        if (childrenVal.isArray()) {
            const QJsonArray childrenArr = childrenVal.toArray();
            node.children.reserve(childrenArr.size());
            for (const auto& val : childrenArr) {
                if (val.isObject()) {
                    node.children.append(ParseFolderTreeNode(val.toObject()));
                }
            }
        }

        return node;
    }

    /**
     * @brief Parse folder tree result from envelope data.
     *
     * @details
     * Expected shape: data = { "tree": [ { "id": N, "name": "...", "children": [...] }, ... ] }
     * Note: the backend may return the tree directly as an array in data, or wrapped in "tree" key.
     */
    inline auto ParseFolderTreeResult(const QJsonValue& dataVal) -> std::optional<FolderTreeResultDto> {
        FolderTreeResultDto result;

        // Try data as array directly (backend returns array at top level)
        if (dataVal.isArray()) {
            const QJsonArray arr = dataVal.toArray();
            result.tree.reserve(arr.size());
            for (const auto& val : arr) {
                if (val.isObject()) {
                    result.tree.append(ParseFolderTreeNode(val.toObject()));
                }
            }
            return result;
        }

        // Try data as object with "tree" key
        if (dataVal.isObject()) {
            const QJsonObject obj = dataVal.toObject();
            const QJsonValue treeVal = obj.value(QLatin1String("tree"));
            if (treeVal.isArray()) {
                const QJsonArray arr = treeVal.toArray();
                result.tree.reserve(arr.size());
                for (const auto& val : arr) {
                    if (val.isObject()) {
                        result.tree.append(ParseFolderTreeNode(val.toObject()));
                    }
                }
                return result;
            }
        }

        return std::nullopt;
    }

    /**
     * @brief Parse create folder result from envelope data.
     *
     * @details
     * Expected shape: data = { "id": N, "name": "...", "parent_id": N, "path": "...", "created_at": "..." }
     */
    inline auto ParseCreateFolderResult(const QJsonValue& dataVal) -> std::optional<CreateFolderResultDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }

        const QJsonObject obj = dataVal.toObject();
        if (!obj.contains(QLatin1String("id"))) {
            return std::nullopt;
        }

        CreateFolderResultDto result;
        result.id = static_cast<quint64>(obj.value(QLatin1String("id")).toDouble());
        result.name = obj.value(QLatin1String("name")).toString();
        result.parentId = static_cast<quint64>(obj.value(QLatin1String("parent_id")).toDouble());
        result.path = obj.value(QLatin1String("path")).toString();
        result.createdAt = obj.value(QLatin1String("created_at")).toString();
        return result;
    }

    /**
     * @brief Parse rename result from envelope data.
     *
     * @details
     * Expected shape: data = { "id": N, "name": "...", "updated_at": "..." }
     */
    inline auto ParseRenameResult(const QJsonValue& dataVal) -> std::optional<RenameResultDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }

        const QJsonObject obj = dataVal.toObject();
        if (!obj.contains(QLatin1String("id"))) {
            return std::nullopt;
        }

        RenameResultDto result;
        result.id = static_cast<quint64>(obj.value(QLatin1String("id")).toDouble());
        result.name = obj.value(QLatin1String("name")).toString();
        result.updatedAt = obj.value(QLatin1String("updated_at")).toString();
        return result;
    }

    /**
     * @brief Parse move result from envelope data.
     *
     * @details
     * Expected shape: data = { "moved_count": N }
     */
    inline auto ParseMoveResult(const QJsonValue& dataVal) -> std::optional<MoveResultDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }

        const QJsonObject obj = dataVal.toObject();

        MoveResultDto result;
        result.movedCount = obj.value(QLatin1String("moved_count")).toInt();
        return result;
    }

    /**
     * @brief Parse copy result from envelope data.
     *
     * @details
     * Expected shape: data = { "copied_count": N, "new_files": [ { "old_id": N, "new_id": N }, ... ] }
     */
    inline auto ParseCopyResult(const QJsonValue& dataVal) -> std::optional<CopyResultDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }

        const QJsonObject obj = dataVal.toObject();

        CopyResultDto result;
        result.copiedCount = obj.value(QLatin1String("copied_count")).toInt();

        const QJsonValue filesVal = obj.value(QLatin1String("new_files"));
        if (filesVal.isArray()) {
            const QJsonArray filesArr = filesVal.toArray();
            result.newFiles.reserve(filesArr.size());
            for (const auto& val : filesArr) {
                if (val.isObject()) {
                    const QJsonObject mappingObj = val.toObject();
                    FileIdMappingDto mapping;
                    mapping.oldId = static_cast<quint64>(mappingObj.value(QLatin1String("old_id")).toDouble());
                    mapping.newId = static_cast<quint64>(mappingObj.value(QLatin1String("new_id")).toDouble());
                    result.newFiles.append(mapping);
                }
            }
        }

        return result;
    }

    /**
     * @brief Parse delete result from envelope data.
     *
     * @details
     * Expected shape: data = { "deleted_count": N }
     */
    inline auto ParseDeleteResult(const QJsonValue& dataVal) -> std::optional<DeleteResultDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }

        const QJsonObject obj = dataVal.toObject();

        DeleteResultDto result;
        result.deletedCount = obj.value(QLatin1String("deleted_count")).toInt();
        return result;
    }

    /**
     * @brief Parse a single search result item from a JSON object.
     */
    inline auto ParseSearchResultItem(const QJsonObject& obj) -> SearchResultItemDto {
        SearchResultItemDto item;
        item.id = static_cast<quint64>(obj.value(QLatin1String("id")).toDouble());
        item.name = obj.value(QLatin1String("name")).toString();
        item.type = obj.value(QLatin1String("type")).toString();
        item.size = static_cast<quint64>(obj.value(QLatin1String("size")).toDouble());
        item.mimeType = obj.value(QLatin1String("mime_type")).toString();
        item.hash = obj.value(QLatin1String("hash")).toString();
        item.itemCount = obj.value(QLatin1String("item_count")).toInt();
        item.createdAt = obj.value(QLatin1String("created_at")).toString();
        item.updatedAt = obj.value(QLatin1String("updated_at")).toString();
        item.path = obj.value(QLatin1String("path")).toString();
        return item;
    }

    /**
     * @brief Parse search result from envelope data.
     *
     * @details
     * Expected shape: data = { "items": [...], "pagination": { ... } }
     */
    inline auto ParseSearchResult(const QJsonValue& dataVal) -> std::optional<SearchResultDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }

        const QJsonObject obj = dataVal.toObject();
        const QJsonValue itemsVal = obj.value(QLatin1String("items"));
        if (!itemsVal.isArray()) {
            return std::nullopt;
        }

        SearchResultDto result;

        const QJsonArray itemsArr = itemsVal.toArray();
        result.items.reserve(itemsArr.size());
        for (const auto& val : itemsArr) {
            if (val.isObject()) {
                result.items.append(ParseSearchResultItem(val.toObject()));
            }
        }

        const QJsonValue pagVal = obj.value(QLatin1String("pagination"));
        if (pagVal.isObject()) {
            result.pagination = ParsePagination(pagVal.toObject());
        }

        return result;
    }

} // namespace disk::qml::models
