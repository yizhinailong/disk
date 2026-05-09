/**
 * @file DriveListModel.cpp
 * @brief DriveListModel implementation
 *
 * @copyright Copyright (c) 2026
 */

#include "models/DriveListModel.hpp"

namespace disk::desktop {

    DriveListModel::DriveListModel(QObject* parent)
        : QAbstractListModel(parent) {}

    auto DriveListModel::rowCount(const QModelIndex& parent) const -> int {
        if (parent.isValid()) {
            return 0;
        }
        return m_items.size();
    }

    auto DriveListModel::data(const QModelIndex& index, int role) const -> QVariant {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
            return {};
        }

        const auto& item = m_items.at(index.row());

        switch (role) {
            case IdRole       : return item.id;
            case KindRole     : return item.kind;
            case NameRole     : return item.name;
            case SizeRole     : return item.size.has_value() ? QVariant(*item.size) : QVariant();
            case MimeTypeRole : return item.mime_type.has_value() ? QVariant(*item.mime_type) : QVariant();
            case HashRole     : return item.hash.has_value() ? QVariant(*item.hash) : QVariant();
            case ItemCountRole: return item.item_count.has_value() ? QVariant(*item.item_count) : QVariant();
            case ParentIdRole : return item.parent_id.has_value() ? QVariant(*item.parent_id) : QVariant();
            case PathRole     : return item.path.has_value() ? QVariant(*item.path) : QVariant();
            case CreatedAtRole: return item.created_at.has_value() ? QVariant(*item.created_at) : QVariant();
            case UpdatedAtRole: return item.updated_at.has_value() ? QVariant(*item.updated_at) : QVariant();
            case OriginRole   : return item.origin;
            default           : return {};
        }
    }

    auto DriveListModel::roleNames() const -> QHash<int, QByteArray> {
        return {
            {        IdRole,        "id" },
            {      KindRole,      "kind" },
            {      NameRole,      "name" },
            {      SizeRole,      "size" },
            {  MimeTypeRole,  "mimeType" },
            {      HashRole,      "hash" },
            { ItemCountRole, "itemCount" },
            {  ParentIdRole,  "parentId" },
            {      PathRole,      "path" },
            { CreatedAtRole, "createdAt" },
            { UpdatedAtRole, "updatedAt" },
            {    OriginRole,    "origin" },
        };
    }

    auto DriveListModel::AddItem(const DriveItem& item) -> void {
        beginInsertRows({}, m_items.size(), m_items.size());
        m_items.append(item);
        endInsertRows();
    }

    auto DriveListModel::AddItems(const QVector<DriveItem>& items) -> void {
        if (items.isEmpty()) {
            return;
        }
        beginInsertRows({}, m_items.size(), m_items.size() + items.size() - 1);
        m_items.append(items);
        endInsertRows();
    }

    auto DriveListModel::RemoveItem(quint64 id) -> bool {
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items[i].id == id) {
                beginRemoveRows({}, i, i);
                m_items.removeAt(i);
                endRemoveRows();
                return true;
            }
        }
        return false;
    }

    auto DriveListModel::Clear() -> void {
        if (m_items.isEmpty()) {
            return;
        }
        beginResetModel();
        m_items.clear();
        endResetModel();
    }

    auto DriveListModel::SetItems(const QVector<DriveItem>& items) -> void {
        beginResetModel();
        m_items = items;
        endResetModel();
    }

} // namespace disk::desktop
