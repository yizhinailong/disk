/**
 * @file AdminShareListModel.cpp
 * @brief AdminShareListModel implementation
 *
 * @copyright Copyright (c) 2026
 */

#include "models/AdminShareListModel.hpp"

#include <QJsonObject>

namespace disk::desktop {

    auto AdminShareItem::FromJson(const QJsonObject& json) -> AdminShareItem {
        AdminShareItem item;
        item.id = json.value("id").toVariant().toULongLong();
        item.user_id = json.value("user_id").toVariant().toULongLong();
        item.username = json.value("username").toString();
        item.file_id = json.value("file_id").toVariant().toULongLong();
        item.file_name = json.value("file_name").toString();
        item.share_code = json.value("share_code").toString();
        item.status = json.value("status").toInt();
        item.access_count = json.value("access_count").toInt();
        item.created_at = json.value("created_at").toString();
        item.expires_at = json.value("expires_at").toString();
        return item;
    }

    AdminShareListModel::AdminShareListModel(QObject* parent)
        : QAbstractListModel(parent) {}

    auto AdminShareListModel::rowCount(const QModelIndex& parent) const -> int {
        if (parent.isValid()) {
            return 0;
        }
        return m_items.size();
    }

    auto AdminShareListModel::data(const QModelIndex& index, int role) const -> QVariant {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
            return {};
        }

        const auto& item = m_items.at(index.row());

        switch (role) {
            case IdRole         : return item.id;
            case UserIdRole     : return item.user_id;
            case UsernameRole   : return item.username;
            case FileIdRole     : return item.file_id;
            case FileNameRole   : return item.file_name;
            case ShareCodeRole  : return item.share_code;
            case StatusRole     : return item.status;
            case AccessCountRole: return item.access_count;
            case CreatedAtRole  : return item.created_at;
            case ExpiresAtRole  : return item.expires_at;
            default             : return {};
        }
    }

    auto AdminShareListModel::roleNames() const -> QHash<int, QByteArray> {
        return {
            { IdRole,         "id" },
            { UserIdRole,     "userId" },
            { UsernameRole,   "username" },
            { FileIdRole,     "fileId" },
            { FileNameRole,   "fileName" },
            { ShareCodeRole,  "shareCode" },
            { StatusRole,     "status" },
            { AccessCountRole,"accessCount" },
            { CreatedAtRole, "createdAt" },
            { ExpiresAtRole, "expiresAt" },
        };
    }

    auto AdminShareListModel::SetItems(const QVector<AdminShareItem>& items) -> void {
        beginResetModel();
        m_items = items;
        endResetModel();
    }

    auto AdminShareListModel::GetItem(int row) -> std::optional<AdminShareItem> {
        if (row < 0 || row >= m_items.size()) {
            return std::nullopt;
        }
        return m_items.at(row);
    }

    auto AdminShareListModel::Clear() -> void {
        if (m_items.isEmpty()) {
            return;
        }
        beginResetModel();
        m_items.clear();
        endResetModel();
    }

} // namespace disk::desktop