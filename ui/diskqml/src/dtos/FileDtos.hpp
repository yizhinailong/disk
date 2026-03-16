/**
 * @file FileDtos.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QML 客户端文件/文件夹响应数据传输对象及 JSON 解析辅助函数
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

    // ==================== 共享结构体 ====================

    /**
     * @brief 列表响应的分页元数据
     *
     * @details
     * 镜像后端 Pagination: { "page", "page_size", "total", "total_pages" }
     */
    struct PaginationDto {
        int page{};
        int pageSize{};
        int total{};
        int totalPages{};
    };

    // ==================== 文件列表 ====================

    /**
     * @brief 文件列表中的单个条目（文件或文件夹）
     *
     * @details
     * 镜像后端 FileListItem。type 为 "file" 或 "folder"。
     * 文件：填充 size、mimeType、hash。
     * 文件夹：填充 itemCount。
     */
    struct FileListItemDto {
        quint64 id{};
        QString name;
        QString type; ///< "file"（文件）或 "folder"（文件夹）
        // 文件字段：
        quint64 size{};
        QString mimeType;
        QString hash;
        // 文件夹字段：
        int itemCount{};
        // 公共字段：
        QString createdAt;
        QString updatedAt;
    };

    /**
     * @brief 文件列表查询结果
     */
    struct FileListResultDto {
        QVector<FileListItemDto> items;
        PaginationDto pagination;
    };

    // ==================== 面包屑 ====================

    /**
     * @brief 单个面包屑条目
     */
    struct BreadcrumbItemDto {
        quint64 id{};
        QString name;
    };

    /**
     * @brief 从根目录到当前文件夹的面包屑路径
     */
    struct BreadcrumbResultDto {
        QVector<BreadcrumbItemDto> path;
    };

    // ==================== 文件夹树 ====================

    /**
     * @brief 文件夹树中的节点
     */
    struct FolderTreeNodeDto {
        quint64 id{};
        QString name;
        QVector<FolderTreeNodeDto> children;
    };

    /**
     * @brief 文件夹树查询结果
     */
    struct FolderTreeResultDto {
        QVector<FolderTreeNodeDto> tree;
    };

    // ==================== 创建文件夹 ====================

    /**
     * @brief 创建文件夹的结果
     */
    struct CreateFolderResultDto {
        quint64 id{};
        QString name;
        quint64 parentId{};
        QString path;
        QString createdAt;
    };

    // ==================== 重命名 ====================

    /**
     * @brief 重命名文件或文件夹的结果
     */
    struct RenameResultDto {
        quint64 id{};
        QString name;
        QString updatedAt;
    };

    // ==================== 移动 ====================

    /**
     * @brief 移动操作的结果
     */
    struct MoveResultDto {
        int movedCount{};
    };

    // ==================== 复制 ====================

    /**
     * @brief 复制文件的 ID 映射
     */
    struct FileIdMappingDto {
        quint64 oldId{};
        quint64 newId{};
    };

    /**
     * @brief 复制操作的结果
     */
    struct CopyResultDto {
        int copiedCount{};
        QVector<FileIdMappingDto> newFiles;
    };

    // ==================== 删除 ====================

    /**
     * @brief 删除操作的结果（软删除到回收站）
     */
    struct DeleteResultDto {
        int deletedCount{};
    };

    // ==================== 搜索 ====================

    /**
     * @brief 单个搜索结果条目
     *
     * @details
     * 扩展 FileListItemDto，添加路径字段用于显示。
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
        QString path; ///< 面包屑路径字符串
    };

    /**
     * @brief 文件搜索查询结果
     */
    struct SearchResultDto {
        QVector<SearchResultItemDto> items;
        PaginationDto pagination;
    };

    // ==================== JSON 解析辅助函数 ====================

    /**
     * @brief 从 JSON 对象解析分页信息
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
     * @brief 从 JSON 对象解析单个 FileListItem
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
     * @brief 从信封数据解析文件列表结果
     *
     * @details
     * 预期格式：data = { "items": [...], "pagination": { ... } }
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
     * @brief 从信封数据解析面包屑结果
     *
     * @details
     * 预期格式：data = { "path": [ { "id": N, "name": "..." }, ... ] }
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
     * @brief 递归解析单个文件夹树节点
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
     * @brief 从信封数据解析文件夹树结果
     *
     * @details
     * 预期格式：data = { "tree": [ { "id": N, "name": "...", "children": [...] }, ... ] }
     * 注意：后端可能直接返回数组作为 data，或包装在 "tree" 键中。
     */
    inline auto ParseFolderTreeResult(const QJsonValue& dataVal) -> std::optional<FolderTreeResultDto> {
        FolderTreeResultDto result;

        // 尝试将 data 作为数组直接解析（后端在顶层返回数组）
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

        // 尝试将 data 作为带 "tree" 键的对象解析
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
     * @brief 从信封数据解析创建文件夹结果
     *
     * @details
     * 预期格式：data = { "id": N, "name": "...", "parent_id": N, "path": "...", "created_at": "..." }
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
     * @brief 从信封数据解析重命名结果
     *
     * @details
     * 预期格式：data = { "id": N, "name": "...", "updated_at": "..." }
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
     * @brief 从信封数据解析移动结果
     *
     * @details
     * 预期格式：data = { "moved_count": N }
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
     * @brief 从信封数据解析复制结果
     *
     * @details
     * 预期格式：data = { "copied_count": N, "new_files": [ { "old_id": N, "new_id": N }, ... ] }
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
     * @brief 从信封数据解析删除结果
     *
     * @details
     * 预期格式：data = { "deleted_count": N }
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
     * @brief 从 JSON 对象解析单个搜索结果条目
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
     * @brief 从信封数据解析搜索结果
     *
     * @details
     * 预期格式：data = { "items": [...], "pagination": { ... } }
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
