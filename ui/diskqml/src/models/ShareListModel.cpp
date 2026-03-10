/**
 * @file ShareListModel.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QAbstractListModel implementation for share items
 *
 * @copyright Copyright (c) 2026
 */

#include "models/ShareListModel.hpp"

namespace disk::qml::models {

    ShareListModel::ShareListModel(QObject* parent)
        : QAbstractListModel(parent) {}

    // ==================== QAbstractListModel interface ====================

    auto ShareListModel::rowCount(const QModelIndex& parent) const -> int {
        if (parent.isValid()) {
            return 0; // flat list — no children
        }
        return static_cast<int>(m_items.size());
    }

    auto ShareListModel::data(const QModelIndex& index, int role) const -> QVariant {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
            return {};
        }

        const auto& item = m_items.at(index.row());

        switch (role) {
            case ShareIdRole           : return item.shareId;
            case ShareFileNameRole     : return item.fileName;
            case ShareFileCountRole    : return item.fileCount;
            case ShareLinkRole         : return item.shareLink;
            case ShareHasPasswordRole  : return item.hasPassword;
            case SharePermissionRole   : return item.permission;
            case ShareViewCountRole    : return item.viewCount;
            case ShareDownloadCountRole: return item.downloadCount;
            case ShareCreatedAtRole    : return item.createdAt;
            case ShareExpiresAtRole    : return item.expiresAt;
            case ShareStatusRole       : return item.status;
            default                    : return {};
        }
    }

    auto ShareListModel::roleNames() const -> QHash<int, QByteArray> {
        static const QHash<int, QByteArray> roles{
            {            ShareIdRole,            "shareId" },
            {      ShareFileNameRole,      "shareFileName" },
            {     ShareFileCountRole,     "shareFileCount" },
            {          ShareLinkRole,          "shareLink" },
            {   ShareHasPasswordRole,   "shareHasPassword" },
            {    SharePermissionRole,    "sharePermission" },
            {     ShareViewCountRole,     "shareViewCount" },
            { ShareDownloadCountRole, "shareDownloadCount" },
            {     ShareCreatedAtRole,     "shareCreatedAt" },
            {     ShareExpiresAtRole,     "shareExpiresAt" },
            {        ShareStatusRole,        "shareStatus" },
        };
        return roles;
    }

    // ==================== Public API ====================

    auto ShareListModel::Count() const -> int {
        return static_cast<int>(m_items.size());
    }

    void ShareListModel::ResetItems(const QVector<ShareListItemData>& items) {
        beginResetModel();
        m_items = items;
        endResetModel();
        emit countChanged();
    }

    void ShareListModel::Clear() {
        if (m_items.isEmpty()) {
            return;
        }
        beginResetModel();
        m_items.clear();
        endResetModel();
        emit countChanged();
    }

    auto ShareListModel::ItemAt(int row) const -> std::optional<ShareListItemData> {
        if (row < 0 || row >= m_items.size()) {
            return std::nullopt;
        }
        return m_items.at(row);
    }

} // namespace disk::qml::models
