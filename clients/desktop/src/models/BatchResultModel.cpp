/**
 * @file BatchResultModel.cpp
 * @brief BatchResultModel implementation
 *
 * @copyright Copyright (c) 2026
 */

#include "models/BatchResultModel.hpp"

#include <QJsonArray>
#include <QJsonObject>

namespace disk::desktop {

    auto BatchActionResultItem::FromJson(const QJsonObject& json, const QString& operation) -> BatchActionResultItem {
        BatchActionResultItem item;

        if (operation == "share_cancel") {
            item.resource_key = json.value("share_id").toString();
        } else {
            item.resource_key = QString::number(static_cast<quint64>(json.value("trash_id").toDouble(0)));
        }

        item.status = json.value("status").toString();

        if (json.contains("file_id") && json["file_id"].toDouble(0) > 0) {
            item.restored_item_id = static_cast<quint64>(json["file_id"].toDouble(0));
            item.restored_item_kind = "file";
        } else if (json.contains("folder_id") && json["folder_id"].toDouble(0) > 0) {
            item.restored_item_id = static_cast<quint64>(json["folder_id"].toDouble(0));
            item.restored_item_kind = "folder";
        }

        if (json.contains("path") && json["path"].isString()) {
            item.resolved_path = json.value("path").toString();
        }

        if (json.contains("freed_space") && json["freed_space"].isDouble()) {
            item.freed_space = static_cast<quint64>(json["freed_space"].toDouble(0));
        }

        if (json.contains("error") && json["error"].isObject()) {
            item.error = ErrorAdapter::FromJson(json["error"].toObject());
        }

        return item;
    }

    auto BatchActionResultItem::ToJson() const -> QJsonObject {
        QJsonObject json;
        json["resource_key"] = resource_key;
        json["status"] = status;

        if (restored_item_id.has_value()) {
            json["restored_item_id"] = static_cast<double>(*restored_item_id);
        }
        if (restored_item_kind.has_value()) {
            json["restored_item_kind"] = *restored_item_kind;
        }
        if (resolved_path.has_value()) {
            json["resolved_path"] = *resolved_path;
        }
        if (freed_space.has_value()) {
            json["freed_space"] = static_cast<double>(*freed_space);
        }

        return json;
    }

    auto BatchActionResult::FromJson(const QJsonObject& json, const QString& operation) -> BatchActionResult {
        BatchActionResult result;
        result.operation = operation;

        const auto summary = json.value("summary").toObject();
        result.total_count = summary.value("total").toInt(0);

        if (summary.contains("succeeded")) {
            result.success_count = summary.value("succeeded").toInt(0);
            result.failure_count = summary.value("failed").toInt(0);
        } else {
            result.success_count = summary.value("success_count").toInt(0);
            result.failure_count = summary.value("failure_count").toInt(0);
        }

        if (json.contains("results") && json["results"].isArray()) {
            const auto arr = json["results"].toArray();
            result.items.reserve(arr.size());
            for (const auto& v : arr) {
                result.items.append(BatchActionResultItem::FromJson(v.toObject(), operation));
            }
        }

        return result;
    }

    auto BatchActionResult::ToJson() const -> QJsonObject {
        QJsonObject json;
        json["operation"] = operation;

        QJsonObject summary;
        summary["total"] = total_count;
        summary["success_count"] = success_count;
        summary["failure_count"] = failure_count;
        json["summary"] = summary;

        QJsonArray results;
        for (const auto& item : items) {
            results.append(item.ToJson());
        }
        json["results"] = results;

        return json;
    }

    BatchResultModel::BatchResultModel(QObject* parent)
        : QAbstractListModel(parent) {}

    auto BatchResultModel::rowCount(const QModelIndex& parent) const -> int {
        if (parent.isValid()) {
            return 0;
        }
        return m_result.items.size();
    }

    auto BatchResultModel::data(const QModelIndex& index, int role) const -> QVariant {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_result.items.size()) {
            return {};
        }

        const auto& item = m_result.items.at(index.row());

        switch (role) {
            case ResourceKeyRole     : return item.resource_key;
            case StatusRole          : return item.status;
            case RestoredItemIdRole  : return item.restored_item_id.has_value() ? QVariant(*item.restored_item_id) : QVariant();
            case RestoredItemKindRole: return item.restored_item_kind.has_value() ? QVariant(*item.restored_item_kind) : QVariant();
            case ResolvedPathRole    : return item.resolved_path.has_value() ? QVariant(*item.resolved_path) : QVariant();
            case FreedSpaceRole      : return item.freed_space.has_value() ? QVariant(*item.freed_space) : QVariant();
            case ErrorRole           : {
                if (!item.error.has_value()) {
                    return QVariant();
                }
                QJsonObject err;
                err["code"] = item.error->code;
                err["message"] = item.error->message;
                err["category"] = item.error->category;
                return err;
            }
            default: return {};
        }
    }

    auto BatchResultModel::roleNames() const -> QHash<int, QByteArray> {
        return {
            {      ResourceKeyRole,      "resourceKey" },
            {           StatusRole,           "status" },
            {   RestoredItemIdRole,   "restoredItemId" },
            { RestoredItemKindRole, "restoredItemKind" },
            {     ResolvedPathRole,     "resolvedPath" },
            {       FreedSpaceRole,       "freedSpace" },
            {            ErrorRole,            "error" },
        };
    }

    auto BatchResultModel::SetResult(const BatchActionResult& result) -> void {
        beginResetModel();

        auto old_op = m_result.operation;
        auto old_total = m_result.total_count;
        auto old_success = m_result.success_count;
        auto old_failure = m_result.failure_count;

        m_result = result;

        if (old_op != m_result.operation) {
            emit operationChanged();
        }
        if (old_total != m_result.total_count) {
            emit totalCountChanged();
        }
        if (old_success != m_result.success_count) {
            emit successCountChanged();
        }
        if (old_failure != m_result.failure_count) {
            emit failureCountChanged();
        }

        endResetModel();
    }

    auto BatchResultModel::Clear() -> void {
        beginResetModel();
        m_result = {};
        emit operationChanged();
        emit totalCountChanged();
        emit successCountChanged();
        emit failureCountChanged();
        endResetModel();
    }

    auto BatchResultModel::GetOperation() const -> QString {
        return m_result.operation;
    }

    auto BatchResultModel::GetTotalCount() const -> int {
        return m_result.total_count;
    }

    auto BatchResultModel::GetSuccessCount() const -> int {
        return m_result.success_count;
    }

    auto BatchResultModel::GetFailureCount() const -> int {
        return m_result.failure_count;
    }

} // namespace disk::desktop
