/**
 * @file DriveItem.hpp
 * @brief Unified file/folder value object per doc 02 §3.5
 *
 * DriveItem is the ONLY mixed item model for file lists, search results,
 * share access, share browse, and share detail views.
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <optional>

namespace disk::desktop {

    struct DriveItem {
        quint64 id{ 0 };
        QString kind; // "file" / "folder"
        QString name;
        std::optional<quint64> size;
        std::optional<QString> mime_type;
        std::optional<QString> hash;
        std::optional<int> item_count;
        std::optional<quint64> parent_id;
        std::optional<QString> path;
        std::optional<QDateTime> created_at;
        std::optional<QDateTime> updated_at;
        QString origin; // "file_list" / "search" / "share_access" / "share_browse" / "share_detail"

        /**
         * @brief Map backend JSON to canonical DriveItem
         *
         * Applies doc 02 §4 normalization rules:
         * - `type` → `kind`
         * - file fields (size, mime_type, hash) only set for kind=="file"
         * - item_count only set for kind=="folder"
         * - share browse folder size=0 → null
         */
        static auto FromJson(const QJsonObject& json, const QString& origin) -> DriveItem;

        auto ToJson() const -> QJsonObject;
    };

} // namespace disk::desktop
