/**
 * @file ShareDtos.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Share response DTOs and JSON parsing helpers for the QML client
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * DTOs mirror the backend definitions in src/dtos/ShareDto.hpp.
 * All types are plain structs (no QObject) with inline Parse functions.
 *
 * Covered endpoints:
 * - POST   /api/share              → CreateShareResultDto
 * - GET    /api/share              → ShareListResultDto
 * - GET    /api/share/{id}         → ShareDetailResultDto
 * - PUT    /api/share/{id}         → UpdateShareResultDto
 * - DELETE /api/share              → CancelShareResultDto
 * - POST   /api/share/access/{id}  → ShareAccessResultDto
 * - GET    /api/share/browse/{id}  → ShareBrowseResultDto
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

    // ==================== Shared Components ====================

    /**
     * @brief A file entry inside a share (used in detail + access responses).
     *
     * @details
     * Mirrors backend ShareFile: { id, name, type, size }
     */
    struct ShareFileDto {
        quint64 id{};
        QString name;
        QString type; ///< "file" or "folder"
        quint64 size{};
    };

    // ==================== Create Share ====================

    /**
     * @brief Result of POST /api/share.
     */
    struct CreateShareResultDto {
        QString shareId;
        QString shareLink;
        std::optional<QString> password;
        QString permission;
        QString expiresAt;
        QString createdAt;
    };

    // ==================== Share List ====================

    /**
     * @brief Pagination metadata (reused from FileDtos pattern).
     *
     * @details
     * The share list response has its own pagination object identical to
     * the file list one. We reuse the same shape but define it locally
     * to avoid a header dependency on FileDtos.hpp.
     */
    struct SharePaginationDto {
        int page{};
        int pageSize{};
        int total{};
        int totalPages{};
    };

    /**
     * @brief A single item in the share list.
     *
     * @details
     * Mirrors backend ShareItem.
     */
    struct ShareListItemDto {
        QString shareId;
        QString fileName;
        int fileCount{};
        QString shareLink;
        bool hasPassword{};
        QString permission;
        int viewCount{};
        int downloadCount{};
        QString createdAt;
        QString expiresAt;
        QString status;
    };

    /**
     * @brief Result of GET /api/share (share list with pagination).
     */
    struct ShareListResultDto {
        QVector<ShareListItemDto> items;
        SharePaginationDto pagination;
    };

    // ==================== Share Detail ====================

    /**
     * @brief Result of GET /api/share/{share_id} (owner detail view).
     *
     * @details
     * Mirrors backend ShareDetailResponse, includes file list.
     */
    struct ShareDetailResultDto {
        QString shareId;
        QVector<ShareFileDto> files;
        QString shareLink;
        bool hasPassword{};
        QString permission;
        int viewCount{};
        int downloadCount{};
        QString createdAt;
        QString expiresAt;
        QString status;
    };

    // ==================== Update Share ====================

    /**
     * @brief Result of PUT /api/share/{share_id}.
     */
    struct UpdateShareResultDto {
        QString shareId;
        QString expiresAt;
        bool hasPassword{};
        QString permission;
        QString updatedAt;
    };

    // ==================== Cancel Share ====================

    /**
     * @brief Error info for a single cancel operation.
     */
    struct CancelShareErrorDto {
        int code{};
        QString message;
        QString reason;
    };

    /**
     * @brief Result for a single share_id in the cancel response.
     */
    struct CancelShareItemResultDto {
        QString shareId;
        QString status; ///< "success" or "failed"
        std::optional<CancelShareErrorDto> error;
    };

    /**
     * @brief Summary counts from the cancel response.
     */
    struct CancelShareSummaryDto {
        int total{};
        int succeeded{};
        int failed{};
    };

    /**
     * @brief Result of DELETE /api/share (batch cancel).
     */
    struct CancelShareResultDto {
        CancelShareSummaryDto summary;
        QVector<CancelShareItemResultDto> results;
    };

    // ==================== Access Share ====================

    /**
     * @brief Result of POST /api/share/access/{share_id}.
     *
     * @details
     * Returns a share_token used for subsequent browse/download requests.
     */
    struct ShareAccessResultDto {
        QString shareToken;
        int expiresIn{};
        QString permission;
        QVector<ShareFileDto> files;
    };

    // ==================== Browse Share ====================

    /**
     * @brief A single item when browsing share content.
     *
     * @details
     * Mirrors backend BrowseItem: { id, name, type, size }
     */
    struct ShareBrowseItemDto {
        quint64 id{};
        QString name;
        QString type; ///< "file" or "folder"
        quint64 size{};
    };

    /**
     * @brief A breadcrumb entry when browsing share content.
     */
    struct ShareBrowseBreadcrumbDto {
        quint64 id{};
        QString name;
    };

    /**
     * @brief Result of GET /api/share/browse/{share_id}.
     */
    struct ShareBrowseResultDto {
        QVector<ShareBrowseItemDto> items;
        QVector<ShareBrowseBreadcrumbDto> breadcrumb;
    };

    // ==================== JSON Parsing Helpers ====================

    /**
     * @brief Parse a ShareFile from a JSON object.
     */
    inline auto ParseShareFile(const QJsonObject& obj) -> ShareFileDto {
        ShareFileDto f;
        f.id = static_cast<quint64>(obj.value(QLatin1String("id")).toDouble());
        f.name = obj.value(QLatin1String("name")).toString();
        f.type = obj.value(QLatin1String("type")).toString();
        f.size = static_cast<quint64>(obj.value(QLatin1String("size")).toDouble());
        return f;
    }

    /**
     * @brief Parse an array of ShareFile objects.
     */
    inline auto ParseShareFileArray(const QJsonValue& val) -> QVector<ShareFileDto> {
        QVector<ShareFileDto> result;
        if (!val.isArray()) {
            return result;
        }
        const QJsonArray arr = val.toArray();
        result.reserve(arr.size());
        for (const auto& item : arr) {
            if (item.isObject()) {
                result.append(ParseShareFile(item.toObject()));
            }
        }
        return result;
    }

    /**
     * @brief Parse CreateShareResultDto from envelope data.
     *
     * @details
     * Expected shape: { "share_id", "share_link", "password"?, "permission",
     *                    "expires_at", "created_at" }
     */
    inline auto ParseCreateShareResult(const QJsonValue& dataVal) -> std::optional<CreateShareResultDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }
        const QJsonObject obj = dataVal.toObject();

        CreateShareResultDto dto;
        dto.shareId = obj.value(QLatin1String("share_id")).toString();
        dto.shareLink = obj.value(QLatin1String("share_link")).toString();

        const QJsonValue pwdVal = obj.value(QLatin1String("password"));
        if (pwdVal.isString()) {
            dto.password = pwdVal.toString();
        }

        dto.permission = obj.value(QLatin1String("permission")).toString();
        dto.expiresAt = obj.value(QLatin1String("expires_at")).toString();
        dto.createdAt = obj.value(QLatin1String("created_at")).toString();
        return dto;
    }

    /**
     * @brief Parse a single ShareListItemDto from a JSON object.
     */
    inline auto ParseShareListItem(const QJsonObject& obj) -> ShareListItemDto {
        ShareListItemDto item;
        item.shareId = obj.value(QLatin1String("share_id")).toString();
        item.fileName = obj.value(QLatin1String("file_name")).toString();
        item.fileCount = obj.value(QLatin1String("file_count")).toInt();
        item.shareLink = obj.value(QLatin1String("share_link")).toString();
        item.hasPassword = obj.value(QLatin1String("has_password")).toBool();
        item.permission = obj.value(QLatin1String("permission")).toString();
        item.viewCount = obj.value(QLatin1String("view_count")).toInt();
        item.downloadCount = obj.value(QLatin1String("download_count")).toInt();
        item.createdAt = obj.value(QLatin1String("created_at")).toString();
        item.expiresAt = obj.value(QLatin1String("expires_at")).toString();
        item.status = obj.value(QLatin1String("status")).toString();
        return item;
    }

    /**
     * @brief Parse share pagination from a JSON object.
     */
    inline auto ParseSharePagination(const QJsonObject& obj) -> SharePaginationDto {
        SharePaginationDto p;
        p.page = obj.value(QLatin1String("page")).toInt();
        p.pageSize = obj.value(QLatin1String("page_size")).toInt();
        p.total = obj.value(QLatin1String("total")).toInt();
        p.totalPages = obj.value(QLatin1String("total_pages")).toInt();
        return p;
    }

    /**
     * @brief Parse ShareListResultDto from envelope data.
     *
     * @details
     * Expected shape: { "items": [...], "pagination": { ... } }
     */
    inline auto ParseShareListResult(const QJsonValue& dataVal) -> std::optional<ShareListResultDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }
        const QJsonObject obj = dataVal.toObject();

        const QJsonValue itemsVal = obj.value(QLatin1String("items"));
        if (!itemsVal.isArray()) {
            return std::nullopt;
        }

        ShareListResultDto result;
        const QJsonArray itemsArr = itemsVal.toArray();
        result.items.reserve(itemsArr.size());
        for (const auto& val : itemsArr) {
            if (val.isObject()) {
                result.items.append(ParseShareListItem(val.toObject()));
            }
        }

        const QJsonValue pagVal = obj.value(QLatin1String("pagination"));
        if (pagVal.isObject()) {
            result.pagination = ParseSharePagination(pagVal.toObject());
        }

        return result;
    }

    /**
     * @brief Parse ShareDetailResultDto from envelope data.
     *
     * @details
     * Expected shape: { "share_id", "files": [...], "share_link", "has_password",
     *                    "permission", "view_count", "download_count",
     *                    "created_at", "expires_at", "status" }
     */
    inline auto ParseShareDetailResult(const QJsonValue& dataVal) -> std::optional<ShareDetailResultDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }
        const QJsonObject obj = dataVal.toObject();

        ShareDetailResultDto dto;
        dto.shareId = obj.value(QLatin1String("share_id")).toString();
        dto.files = ParseShareFileArray(obj.value(QLatin1String("files")));
        dto.shareLink = obj.value(QLatin1String("share_link")).toString();
        dto.hasPassword = obj.value(QLatin1String("has_password")).toBool();
        dto.permission = obj.value(QLatin1String("permission")).toString();
        dto.viewCount = obj.value(QLatin1String("view_count")).toInt();
        dto.downloadCount = obj.value(QLatin1String("download_count")).toInt();
        dto.createdAt = obj.value(QLatin1String("created_at")).toString();
        dto.expiresAt = obj.value(QLatin1String("expires_at")).toString();
        dto.status = obj.value(QLatin1String("status")).toString();
        return dto;
    }

    /**
     * @brief Parse UpdateShareResultDto from envelope data.
     *
     * @details
     * Expected shape: { "share_id", "expires_at", "has_password", "permission", "updated_at" }
     */
    inline auto ParseUpdateShareResult(const QJsonValue& dataVal) -> std::optional<UpdateShareResultDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }
        const QJsonObject obj = dataVal.toObject();

        UpdateShareResultDto dto;
        dto.shareId = obj.value(QLatin1String("share_id")).toString();
        dto.expiresAt = obj.value(QLatin1String("expires_at")).toString();
        dto.hasPassword = obj.value(QLatin1String("has_password")).toBool();
        dto.permission = obj.value(QLatin1String("permission")).toString();
        dto.updatedAt = obj.value(QLatin1String("updated_at")).toString();
        return dto;
    }

    /**
     * @brief Parse CancelShareResultDto from envelope data.
     *
     * @details
     * Expected shape: { "summary": { "total", "succeeded", "failed" },
     *                    "results": [ { "share_id", "status", "error"? } ] }
     */
    inline auto ParseCancelShareResult(const QJsonValue& dataVal) -> std::optional<CancelShareResultDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }
        const QJsonObject obj = dataVal.toObject();

        CancelShareResultDto dto;

        // Parse summary
        const QJsonValue sumVal = obj.value(QLatin1String("summary"));
        if (sumVal.isObject()) {
            const QJsonObject sumObj = sumVal.toObject();
            dto.summary.total = sumObj.value(QLatin1String("total")).toInt();
            dto.summary.succeeded = sumObj.value(QLatin1String("succeeded")).toInt();
            dto.summary.failed = sumObj.value(QLatin1String("failed")).toInt();
        }

        // Parse results
        const QJsonValue resultsVal = obj.value(QLatin1String("results"));
        if (resultsVal.isArray()) {
            const QJsonArray resultsArr = resultsVal.toArray();
            dto.results.reserve(resultsArr.size());
            for (const auto& rVal : resultsArr) {
                if (!rVal.isObject()) {
                    continue;
                }
                const QJsonObject rObj = rVal.toObject();

                CancelShareItemResultDto item;
                item.shareId = rObj.value(QLatin1String("share_id")).toString();
                item.status = rObj.value(QLatin1String("status")).toString();

                const QJsonValue errVal = rObj.value(QLatin1String("error"));
                if (errVal.isObject()) {
                    const QJsonObject errObj = errVal.toObject();
                    CancelShareErrorDto err;
                    err.code = errObj.value(QLatin1String("code")).toInt();
                    err.message = errObj.value(QLatin1String("message")).toString();
                    err.reason = errObj.value(QLatin1String("reason")).toString();
                    item.error = std::move(err);
                }

                dto.results.append(std::move(item));
            }
        }

        return dto;
    }

    /**
     * @brief Parse ShareAccessResultDto from envelope data.
     *
     * @details
     * Expected shape: { "share_token", "expires_in", "permission", "files": [...] }
     */
    inline auto ParseShareAccessResult(const QJsonValue& dataVal) -> std::optional<ShareAccessResultDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }
        const QJsonObject obj = dataVal.toObject();

        ShareAccessResultDto dto;
        dto.shareToken = obj.value(QLatin1String("share_token")).toString();
        dto.expiresIn = obj.value(QLatin1String("expires_in")).toInt();
        dto.permission = obj.value(QLatin1String("permission")).toString();
        dto.files = ParseShareFileArray(obj.value(QLatin1String("files")));
        return dto;
    }

    /**
     * @brief Parse a ShareBrowseItemDto from a JSON object.
     */
    inline auto ParseShareBrowseItem(const QJsonObject& obj) -> ShareBrowseItemDto {
        ShareBrowseItemDto item;
        item.id = static_cast<quint64>(obj.value(QLatin1String("id")).toDouble());
        item.name = obj.value(QLatin1String("name")).toString();
        item.type = obj.value(QLatin1String("type")).toString();
        item.size = static_cast<quint64>(obj.value(QLatin1String("size")).toDouble());
        return item;
    }

    /**
     * @brief Parse a ShareBrowseBreadcrumbDto from a JSON object.
     */
    inline auto ParseShareBrowseBreadcrumb(const QJsonObject& obj) -> ShareBrowseBreadcrumbDto {
        ShareBrowseBreadcrumbDto bc;
        bc.id = static_cast<quint64>(obj.value(QLatin1String("id")).toDouble());
        bc.name = obj.value(QLatin1String("name")).toString();
        return bc;
    }

    /**
     * @brief Parse ShareBrowseResultDto from envelope data.
     *
     * @details
     * Expected shape: { "items": [...], "breadcrumb": [...] }
     */
    inline auto ParseShareBrowseResult(const QJsonValue& dataVal) -> std::optional<ShareBrowseResultDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }
        const QJsonObject obj = dataVal.toObject();

        ShareBrowseResultDto dto;

        const QJsonValue itemsVal = obj.value(QLatin1String("items"));
        if (itemsVal.isArray()) {
            const QJsonArray itemsArr = itemsVal.toArray();
            dto.items.reserve(itemsArr.size());
            for (const auto& val : itemsArr) {
                if (val.isObject()) {
                    dto.items.append(ParseShareBrowseItem(val.toObject()));
                }
            }
        }

        const QJsonValue bcVal = obj.value(QLatin1String("breadcrumb"));
        if (bcVal.isArray()) {
            const QJsonArray bcArr = bcVal.toArray();
            dto.breadcrumb.reserve(bcArr.size());
            for (const auto& val : bcArr) {
                if (val.isObject()) {
                    dto.breadcrumb.append(ParseShareBrowseBreadcrumb(val.toObject()));
                }
            }
        }

        return dto;
    }

} // namespace disk::qml::models
