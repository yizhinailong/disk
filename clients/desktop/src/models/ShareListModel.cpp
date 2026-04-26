/**
 * @file ShareListModel.cpp
 * @brief ShareListModel implementation
 *
 * @copyright Copyright (c) 2026
 */

#include "models/ShareListModel.hpp"

#include <QJsonObject>

namespace disk::desktop {

    auto ShareItem::FromJson(const QJsonObject& json) -> ShareItem {
        ShareItem item;
        item.share_id = json.value("share_id").toString();
        item.share_link = json.value("share_link").toString();
        item.permission = json.value("permission").toString("view");
        item.has_password = json.value("has_password").toBool(false);

        if (json.contains("created_at") && json["created_at"].isString()) {
            item.created_at = QDateTime::fromString(json["created_at"].toString(), Qt::ISODate);
        }
        if (json.contains("expires_at") && json["expires_at"].isString()) {
            item.expires_at = QDateTime::fromString(json["expires_at"].toString(), Qt::ISODate);
        }
        if (json.contains("status")) {
            item.status = json.value("status").toString();
        }
        if (json.contains("view_count")) {
            item.view_count = json.value("view_count").toInt();
        }
        if (json.contains("download_count")) {
            item.download_count = json.value("download_count").toInt();
        }
        if (json.contains("file_name")) {
            item.primary_item_name = json.value("file_name").toString();
        }
        if (json.contains("file_count")) {
            item.item_count = json.value("file_count").toInt();
        }
        if (json.contains("updated_at") && json["updated_at"].isString()) {
            item.updated_at = QDateTime::fromString(json["updated_at"].toString(), Qt::ISODate);
        }

        return item;
    }

    auto ShareItem::ToJson() const -> QJsonObject {
        QJsonObject json;
        json["share_id"] = share_id;
        json["share_link"] = share_link;
        json["permission"] = permission;
        json["has_password"] = has_password;

        if (created_at.has_value()) {
            json["created_at"] = created_at->toString(Qt::ISODate);
        }
        if (expires_at.has_value()) {
            json["expires_at"] = expires_at->toString(Qt::ISODate);
        }
        if (status.has_value()) {
            json["status"] = *status;
        }
        if (view_count.has_value()) {
            json["view_count"] = *view_count;
        }
        if (download_count.has_value()) {
            json["download_count"] = *download_count;
        }
        if (primary_item_name.has_value()) {
            json["file_name"] = *primary_item_name;
        }
        if (item_count.has_value()) {
            json["file_count"] = *item_count;
        }
        if (updated_at.has_value()) {
            json["updated_at"] = updated_at->toString(Qt::ISODate);
        }

        return json;
    }

    ShareListModel::ShareListModel(QObject* parent)
        : QAbstractListModel(parent) {}

    auto ShareListModel::rowCount(const QModelIndex& parent) const -> int {
        if (parent.isValid()) {
            return 0;
        }
        return m_items.size();
    }

    auto ShareListModel::data(const QModelIndex& index, int role) const -> QVariant {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
            return {};
        }

        const auto& item = m_items.at(index.row());

        switch (role) {
            case ShareIdRole        : return item.share_id;
            case ShareLinkRole      : return item.share_link;
            case PermissionRole     : return item.permission;
            case HasPasswordRole    : return item.has_password;
            case CreatedAtRole      : return item.created_at.has_value() ? QVariant(*item.created_at) : QVariant();
            case ExpiresAtRole      : return item.expires_at.has_value() ? QVariant(*item.expires_at) : QVariant();
            case StatusRole         : return item.status.has_value() ? QVariant(*item.status) : QVariant();
            case ViewCountRole      : return item.view_count.has_value() ? QVariant(*item.view_count) : QVariant();
            case DownloadCountRole  : return item.download_count.has_value() ? QVariant(*item.download_count) : QVariant();
            case PrimaryItemNameRole: return item.primary_item_name.has_value() ? QVariant(*item.primary_item_name) : QVariant();
            case ItemCountRole      : return item.item_count.has_value() ? QVariant(*item.item_count) : QVariant();
            case UpdatedAtRole      : return item.updated_at.has_value() ? QVariant(*item.updated_at) : QVariant();
            default                 : return {};
        }
    }

    auto ShareListModel::roleNames() const -> QHash<int, QByteArray> {
        return {
            {         ShareIdRole,         "shareId" },
            {       ShareLinkRole,       "shareLink" },
            {      PermissionRole,      "permission" },
            {     HasPasswordRole,     "hasPassword" },
            {       CreatedAtRole,       "createdAt" },
            {       ExpiresAtRole,       "expiresAt" },
            {          StatusRole,          "status" },
            {       ViewCountRole,       "viewCount" },
            {   DownloadCountRole,   "downloadCount" },
            { PrimaryItemNameRole, "primaryItemName" },
            {       ItemCountRole,       "itemCount" },
            {       UpdatedAtRole,       "updatedAt" },
        };
    }

    auto ShareListModel::AddItem(const ShareItem& item) -> void {
        beginInsertRows({}, m_items.size(), m_items.size());
        m_items.append(item);
        endInsertRows();
    }

    auto ShareListModel::RemoveItem(const QString& share_id) -> bool {
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items[i].share_id == share_id) {
                beginRemoveRows({}, i, i);
                m_items.removeAt(i);
                endRemoveRows();
                return true;
            }
        }
        return false;
    }

    auto ShareListModel::Clear() -> void {
        if (m_items.isEmpty()) {
            return;
        }
        beginResetModel();
        m_items.clear();
        endResetModel();
    }

    auto ShareListModel::GetItem(int row) const -> std::optional<ShareItem> {
        if (row < 0 || row >= m_items.size()) {
            return std::nullopt;
        }
        return m_items.at(row);
    }

    auto ShareListModel::SetItems(const QVector<ShareItem>& items) -> void {
        beginResetModel();
        m_items = items;
        endResetModel();
    }

    auto ShareListModel::indexOf(const QString& share_id) const -> int {
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items[i].share_id == share_id) {
                return i;
            }
        }
        return -1;
    }

} // namespace disk::desktop
