/**
 * @file BreadcrumbModel.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QAbstractListModel implementation for breadcrumb navigation
 *
 * @copyright Copyright (c) 2026
 */

#include "models/BreadcrumbModel.hpp"

namespace disk::qml::models {

    BreadcrumbModel::BreadcrumbModel(QObject* parent)
        : QAbstractListModel(parent) {}

    // ==================== QAbstractListModel interface ====================

    auto BreadcrumbModel::rowCount(const QModelIndex& parent) const -> int {
        if (parent.isValid()) {
            return 0; // flat list — no children
        }
        return static_cast<int>(m_path.size());
    }

    auto BreadcrumbModel::data(const QModelIndex& index, int role) const -> QVariant {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_path.size()) {
            return {};
        }

        const auto& item = m_path.at(index.row());

        switch (role) {
            case FolderIdRole  : return QVariant::fromValue(item.id);
            case FolderNameRole: return item.name;
            default            : return {};
        }
    }

    auto BreadcrumbModel::roleNames() const -> QHash<int, QByteArray> {
        static const QHash<int, QByteArray> roles{
            {   FolderIdRole,   "folderId" },
            { FolderNameRole, "folderName" },
        };
        return roles;
    }

    // ==================== Public API ====================

    auto BreadcrumbModel::Count() const -> int {
        return static_cast<int>(m_path.size());
    }

    void BreadcrumbModel::ResetPath(const QVector<BreadcrumbItemData>& path) {
        beginResetModel();
        m_path = path;
        endResetModel();
        emit countChanged();
    }

    void BreadcrumbModel::Clear() {
        if (m_path.isEmpty()) {
            return;
        }
        beginResetModel();
        m_path.clear();
        endResetModel();
        emit countChanged();
    }

    auto BreadcrumbModel::CurrentFolderId() const -> quint64 {
        if (m_path.isEmpty()) {
            return 0;
        }
        return m_path.last().id;
    }

    auto BreadcrumbModel::ItemAt(int row) const -> std::optional<BreadcrumbItemData> {
        if (row < 0 || row >= m_path.size()) {
            return std::nullopt;
        }
        return m_path.at(row);
    }

} // namespace disk::qml::models
