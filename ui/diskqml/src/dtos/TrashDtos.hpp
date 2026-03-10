/**
 * @file TrashDtos.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Trash response DTOs and JSON parsing helpers for the QML client
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
#include <dtos/FileDtos.hpp>

namespace disk::qml::models {

    // ==================== Trash Item ====================

    /**
     * @brief A single trash item returned by GET /api/trash.
     *
     * @details
     * Mirrors backend TrashItemResponse:
     * { "id", "type", "original_id", "name", "size", "original_path", "deleted_at", "expires_at" }
     */
    struct TrashItemDto {
        quint64 id{};
        QString type; ///< "file" or "folder"
        quint64 originalId{};
        QString name;
        quint64 size{};
        QString originalPath;
        QString deletedAt;
        QString expiresAt;
    };

    /**
     * @brief Result of a trash list query (paginated).
     */
    struct TrashListResultDto {
        QVector<TrashItemDto> items;
        PaginationDto pagination;
    };

    // ==================== Batch Operation Result ====================

    /**
     * @brief Per-item result in a batch restore/delete response.
     */
    struct TrashBatchItemResultDto {
        quint64 trashId{};
        bool success{};
        QString message;
    };

    /**
     * @brief Summary of a batch restore/delete operation.
     */
    struct TrashBatchSummaryDto {
        int total{};
        int successCount{};
        int failureCount{};
    };

    /**
     * @brief Result of POST /api/trash/restore or DELETE /api/trash.
     */
    struct TrashBatchResultDto {
        TrashBatchSummaryDto summary;
        QVector<TrashBatchItemResultDto> results;
    };

    // ==================== Clear All Result ====================

    /**
     * @brief Result of DELETE /api/trash/all.
     */
    struct TrashClearResultDto {
        int deletedCount{};
        quint64 freedSpace{};
    };

    // ==================== Parse Functions ====================

    /**
     * @brief Parse a single trash item from a JSON object.
     */
    inline auto ParseTrashItem(const QJsonObject& obj) -> TrashItemDto {
        TrashItemDto item;
        item.id = static_cast<quint64>(obj.value(QLatin1String("id")).toDouble());
        item.type = obj.value(QLatin1String("type")).toString();
        item.originalId = static_cast<quint64>(obj.value(QLatin1String("original_id")).toDouble());
        item.name = obj.value(QLatin1String("name")).toString();
        item.size = static_cast<quint64>(obj.value(QLatin1String("size")).toDouble());
        item.originalPath = obj.value(QLatin1String("original_path")).toString();
        item.deletedAt = obj.value(QLatin1String("deleted_at")).toString();
        item.expiresAt = obj.value(QLatin1String("expires_at")).toString();
        return item;
    }

    /**
     * @brief Parse trash list result from envelope data.
     *
     * @details
     * Expected shape: data = { "items": [...], "pagination": { ... } }
     */
    inline auto ParseTrashListResult(const QJsonValue& dataVal) -> std::optional<TrashListResultDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }

        const QJsonObject obj = dataVal.toObject();
        const QJsonValue itemsVal = obj.value(QLatin1String("items"));
        if (!itemsVal.isArray()) {
            return std::nullopt;
        }

        TrashListResultDto result;

        const QJsonArray itemsArr = itemsVal.toArray();
        result.items.reserve(itemsArr.size());
        for (const auto& val : itemsArr) {
            if (val.isObject()) {
                result.items.append(ParseTrashItem(val.toObject()));
            }
        }

        const QJsonValue pagVal = obj.value(QLatin1String("pagination"));
        if (pagVal.isObject()) {
            result.pagination = ParsePagination(pagVal.toObject());
        }

        return result;
    }

    /**
     * @brief Parse batch result from envelope data (restore or delete).
     *
     * @details
     * Expected shape: data = {
     *   "summary": { "total": N, "success_count": N, "failure_count": N },
     *   "results": [ { "trash_id": N, "success": bool, "message": "..." }, ... ]
     * }
     */
    inline auto ParseTrashBatchResult(const QJsonValue& dataVal) -> std::optional<TrashBatchResultDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }

        const QJsonObject obj = dataVal.toObject();

        TrashBatchResultDto result;

        const QJsonValue summaryVal = obj.value(QLatin1String("summary"));
        if (summaryVal.isObject()) {
            const QJsonObject summaryObj = summaryVal.toObject();
            result.summary.total = summaryObj.value(QLatin1String("total")).toInt();
            result.summary.successCount = summaryObj.value(QLatin1String("success_count")).toInt();
            result.summary.failureCount = summaryObj.value(QLatin1String("failure_count")).toInt();
        }

        const QJsonValue resultsVal = obj.value(QLatin1String("results"));
        if (resultsVal.isArray()) {
            const QJsonArray resultsArr = resultsVal.toArray();
            result.results.reserve(resultsArr.size());
            for (const auto& val : resultsArr) {
                if (val.isObject()) {
                    const QJsonObject itemObj = val.toObject();
                    TrashBatchItemResultDto itemResult;
                    itemResult.trashId = static_cast<quint64>(itemObj.value(QLatin1String("trash_id")).toDouble());
                    itemResult.success = itemObj.value(QLatin1String("success")).toBool();
                    itemResult.message = itemObj.value(QLatin1String("message")).toString();
                    result.results.append(itemResult);
                }
            }
        }

        return result;
    }

    /**
     * @brief Parse clear-all result from envelope data.
     *
     * @details
     * Expected shape: data = { "deleted_count": N, "freed_space": N }
     */
    inline auto ParseTrashClearResult(const QJsonValue& dataVal) -> std::optional<TrashClearResultDto> {
        if (!dataVal.isObject()) {
            return std::nullopt;
        }

        const QJsonObject obj = dataVal.toObject();

        TrashClearResultDto result;
        result.deletedCount = obj.value(QLatin1String("deleted_count")).toInt();
        result.freedSpace = static_cast<quint64>(obj.value(QLatin1String("freed_space")).toDouble());
        return result;
    }

} // namespace disk::qml::models
