/**
 * @file AdminUserListModel.cpp
 * @brief AdminUserListModel implementation
 *
 * @copyright Copyright (c) 2026
 */

#include "models/AdminUserListModel.hpp"

#include <QJsonObject>

namespace disk::desktop {

    auto AdminUserItem::FromJson(const QJsonObject& json) -> AdminUserItem {
        AdminUserItem item;
        item.id = static_cast<quint64>(json.value("id").toDouble(0));
        item.username = json.value("username").toString();
        item.email = json.value("email").toString();
        item.nickname = json.value("nickname").toString();
        item.role = json.value("role").toInt(0);
        item.status = json.value("status").toInt(1);
        item.storage_quota = static_cast<qint64>(json.value("storage_quota").toDouble(0));
        item.storage_used = static_cast<qint64>(json.value("storage_used").toDouble(0));
        item.created_at = json.value("created_at").toString();
        item.last_login_at = json.value("last_login_at").toString();
        return item;
    }

    AdminUserListModel::AdminUserListModel(QObject* parent)
        : QAbstractListModel(parent) {}

    auto AdminUserListModel::rowCount(const QModelIndex& parent) const -> int {
        if (parent.isValid()) {
            return 0;
        }
        return m_items.size();
    }

    auto AdminUserListModel::data(const QModelIndex& index, int role) const -> QVariant {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
            return {};
        }

        const auto& item = m_items.at(index.row());

        switch (role) {
            case IdRole           : return item.id;
            case UsernameRole     : return item.username;
            case EmailRole        : return item.email;
            case NicknameRole     : return item.nickname;
            case RoleRole         : return item.role;
            case StatusRole       : return item.status;
            case StorageQuotaRole : return item.storage_quota;
            case StorageUsedRole  : return item.storage_used;
            case CreatedAtRole    : return item.created_at;
            case LastLoginAtRole   : return item.last_login_at;
            default               : return {};
        }
    }

    auto AdminUserListModel::roleNames() const -> QHash<int, QByteArray> {
        return {
            { IdRole,           "id" },
            { UsernameRole,     "username" },
            { EmailRole,        "email" },
            { NicknameRole,     "nickname" },
            { RoleRole,         "role" },
            { StatusRole,       "status" },
            { StorageQuotaRole, "storageQuota" },
            { StorageUsedRole,  "storageUsed" },
            { CreatedAtRole,    "createdAt" },
            { LastLoginAtRole,  "lastLoginAt" },
        };
    }

    auto AdminUserListModel::SetItems(const QVector<AdminUserItem>& items) -> void {
        beginResetModel();
        m_items = items;
        endResetModel();
    }

    auto AdminUserListModel::GetItem(int row) -> std::optional<AdminUserItem> {
        if (row < 0 || row >= m_items.size()) {
            return std::nullopt;
        }
        return m_items.at(row);
    }

    auto AdminUserListModel::Clear() -> void {
        if (m_items.isEmpty()) {
            return;
        }
        beginResetModel();
        m_items.clear();
        endResetModel();
    }

} // namespace disk::desktop