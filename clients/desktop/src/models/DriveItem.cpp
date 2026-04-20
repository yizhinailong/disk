/**
 * @file DriveItem.cpp
 * @brief DriveItem value object implementation with JSON mapping per doc 02 §4
 *
 * @copyright Copyright (c) 2026
 */

#include "models/DriveItem.hpp"

namespace disk::desktop {

    auto DriveItem::FromJson(const QJsonObject& json, const QString& origin) -> DriveItem {
        DriveItem item;
        item.id = static_cast<quint64>(json.value("id").toDouble(0));
        item.kind = json.value("type").toString();
        item.name = json.value("name").toString();
        item.origin = origin;

        if (item.kind == "file") {
            if (json.contains("size")) {
                auto size_val = json.value("size").toDouble(-1);
                if (size_val >= 0) {
                    item.size = static_cast<quint64>(size_val);
                }
            }
            if (json.contains("mime_type")) {
                item.mime_type = json.value("mime_type").toString();
            }
            if (json.contains("hash")) {
                item.hash = json.value("hash").toString();
            }
        } else if (item.kind == "folder") {
            if (json.contains("item_count")) {
                item.item_count = json.value("item_count").toInt();
            }

            if (json.contains("size")) {
                auto size_val = json.value("size").toDouble(-1);
                if (size_val == 0 && (origin == "share_browse")) {
                    item.size = std::nullopt;
                } else if (size_val >= 0) {
                    item.size = static_cast<quint64>(size_val);
                }
            }
        }

        if (json.contains("parent_id")) {
            item.parent_id = static_cast<quint64>(json.value("parent_id").toDouble(0));
        }
        if (json.contains("path")) {
            item.path = json.value("path").toString();
        }
        if (json.contains("created_at") && json["created_at"].isString()) {
            item.created_at = QDateTime::fromString(json["created_at"].toString(), Qt::ISODate);
        }
        if (json.contains("updated_at") && json["updated_at"].isString()) {
            item.updated_at = QDateTime::fromString(json["updated_at"].toString(), Qt::ISODate);
        }

        return item;
    }

    auto DriveItem::ToJson() const -> QJsonObject {
        QJsonObject json;
        json["id"] = static_cast<double>(id);
        json["type"] = kind;
        json["name"] = name;
        json["origin"] = origin;

        if (size.has_value()) {
            json["size"] = static_cast<double>(*size);
        }
        if (mime_type.has_value()) {
            json["mime_type"] = *mime_type;
        }
        if (hash.has_value()) {
            json["hash"] = *hash;
        }
        if (item_count.has_value()) {
            json["item_count"] = *item_count;
        }
        if (parent_id.has_value()) {
            json["parent_id"] = static_cast<double>(*parent_id);
        }
        if (path.has_value()) {
            json["path"] = *path;
        }
        if (created_at.has_value()) {
            json["created_at"] = created_at->toString(Qt::ISODate);
        }
        if (updated_at.has_value()) {
            json["updated_at"] = updated_at->toString(Qt::ISODate);
        }

        return json;
    }

} // namespace disk::desktop
