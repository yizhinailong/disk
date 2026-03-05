/**
 * @file TrashListModel.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QAbstractListModel implementation for trash items
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 */

#include "models/TrashListModel.hpp"

namespace disk::qml::models {

    TrashListModel::TrashListModel(QObject* parent)
        : QAbstractListModel(parent) {}

    // ==================== QAbstractListModel interface ====================

    auto TrashListModel::rowCount(const QModelIndex& parent) const -> int {
        if (parent.isValid()) {
            return 0; // flat list — no children
        }
        return static_cast<int>(m_items.size());
    }

    auto TrashListModel::data(const QModelIndex& index, int role) const -> QVariant {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
            return {};
        }

        const auto& item = m_items.at(index.row());

        switch (role) {
            case TrashIdRole          : return QVariant::fromValue(item.id);
            case TrashTypeRole        : return item.type;
            case TrashOriginalIdRole  : return QVariant::fromValue(item.originalId);
            case TrashNameRole        : return item.name;
            case TrashSizeRole        : return QVariant::fromValue(item.size);
            case TrashOriginalPathRole: return item.originalPath;
            case TrashDeletedAtRole   : return item.deletedAt;
            case TrashExpiresAtRole   : return item.expiresAt;
            case TrashIsFolderRole    : return (item.type == QStringLiteral("folder"));
            default                   : return {};
        }
    }

    auto TrashListModel::roleNames() const -> QHash<int, QByteArray> {
        static const QHash<int, QByteArray> roles{
            {           TrashIdRole,           "trashId" },
            {         TrashTypeRole,         "trashType" },
            {   TrashOriginalIdRole,   "trashOriginalId" },
            {         TrashNameRole,         "trashName" },
            {         TrashSizeRole,         "trashSize" },
            { TrashOriginalPathRole, "trashOriginalPath" },
            {    TrashDeletedAtRole,    "trashDeletedAt" },
            {    TrashExpiresAtRole,    "trashExpiresAt" },
            {     TrashIsFolderRole,     "trashIsFolder" },
        };
        return roles;
    }

    // ==================== Public API ====================

    auto TrashListModel::Count() const -> int {
        return static_cast<int>(m_items.size());
    }

    void TrashListModel::ResetItems(const QVector<TrashListItemData>& items) {
        beginResetModel();
        m_items = items;
        endResetModel();
        emit countChanged();
    }

    void TrashListModel::Clear() {
        if (m_items.isEmpty()) {
            return;
        }
        beginResetModel();
        m_items.clear();
        endResetModel();
        emit countChanged();
    }

    auto TrashListModel::ItemAt(int row) const -> std::optional<TrashListItemData> {
        if (row < 0 || row >= m_items.size()) {
            return std::nullopt;
        }
        return m_items.at(row);
    }

} // namespace disk::qml::models
