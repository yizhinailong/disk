/**
 * @file TrashListModel.cpp
 * @brief TrashListModel implementation
 *
 * @copyright Copyright (c) 2026
 */

#include "models/TrashListModel.hpp"

#include <QJsonObject>

namespace disk::desktop {

    auto TrashItem::FromJson(const QJsonObject& json) -> TrashItem {
        TrashItem item;
        item.trash_id = static_cast<quint64>(json.value("id").toDouble(0));
        item.kind = json.value("type").toString();
        item.original_id = static_cast<quint64>(json.value("original_id").toDouble(0));
        item.name = json.value("name").toString();
        item.size = static_cast<quint64>(json.value("size").toDouble(0));
        item.original_path = json.value("original_path").toString();

        if (json.contains("deleted_at") && json["deleted_at"].isString()) {
            item.deleted_at = QDateTime::fromString(json["deleted_at"].toString(), Qt::ISODate);
        }
        if (json.contains("expires_at") && json["expires_at"].isString()) {
            item.expires_at = QDateTime::fromString(json["expires_at"].toString(), Qt::ISODate);
        }

        return item;
    }

    auto TrashItem::ToJson() const -> QJsonObject {
        QJsonObject json;
        json["id"] = static_cast<double>(trash_id);
        json["type"] = kind;
        json["original_id"] = static_cast<double>(original_id);
        json["name"] = name;
        json["size"] = static_cast<double>(size);
        json["original_path"] = original_path;
        json["deleted_at"] = deleted_at.toString(Qt::ISODate);
        json["expires_at"] = expires_at.toString(Qt::ISODate);
        return json;
    }

    TrashListModel::TrashListModel(QObject* parent)
        : QAbstractListModel(parent) {}

    auto TrashListModel::rowCount(const QModelIndex& parent) const -> int {
        if (parent.isValid()) {
            return 0;
        }
        return m_items.size();
    }

    auto TrashListModel::data(const QModelIndex& index, int role) const -> QVariant {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
            return {};
        }

        const auto& item = m_items.at(index.row());

        switch (role) {
            case TrashIdRole     : return item.trash_id;
            case KindRole        : return item.kind;
            case OriginalIdRole  : return item.original_id;
            case NameRole        : return item.name;
            case SizeRole        : return item.size;
            case OriginalPathRole: return item.original_path;
            case DeletedAtRole   : return item.deleted_at;
            case ExpiresAtRole   : return item.expires_at;
            default              : return {};
        }
    }

    auto TrashListModel::roleNames() const -> QHash<int, QByteArray> {
        return {
            {      TrashIdRole,      "trashId" },
            {         KindRole,         "kind" },
            {   OriginalIdRole,   "originalId" },
            {         NameRole,         "name" },
            {         SizeRole,         "size" },
            { OriginalPathRole, "originalPath" },
            {    DeletedAtRole,    "deletedAt" },
            {    ExpiresAtRole,    "expiresAt" },
        };
    }

    auto TrashListModel::AddItem(const TrashItem& item) -> void {
        beginInsertRows({}, m_items.size(), m_items.size());
        m_items.append(item);
        endInsertRows();
    }

    auto TrashListModel::RemoveItem(quint64 trash_id) -> bool {
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items[i].trash_id == trash_id) {
                beginRemoveRows({}, i, i);
                m_items.removeAt(i);
                endRemoveRows();
                return true;
            }
        }
        return false;
    }

    auto TrashListModel::Clear() -> void {
        if (m_items.isEmpty()) {
            return;
        }
        beginResetModel();
        m_items.clear();
        endResetModel();
    }

    auto TrashListModel::GetItem(int row) const -> std::optional<TrashItem> {
        if (row < 0 || row >= m_items.size()) {
            return std::nullopt;
        }
        return m_items.at(row);
    }

    auto TrashListModel::SetItems(const QVector<TrashItem>& items) -> void {
        beginResetModel();
        m_items = items;
        endResetModel();
    }

    auto TrashListModel::indexOf(quint64 trash_id) const -> int {
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items[i].trash_id == trash_id) {
                return i;
            }
        }
        return -1;
    }

} // namespace disk::desktop
