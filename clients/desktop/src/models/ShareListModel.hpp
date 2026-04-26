/**
 * @file ShareListModel.hpp
 * @brief QAbstractListModel for share items per doc 02 §3.9
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QVector>

#include "models/DriveItem.hpp"

namespace disk::desktop {

    struct ShareItem {
        QString share_id;
        QString share_link;
        QString permission; // "view" / "download"
        bool has_password{ false };
        std::optional<QDateTime> created_at;
        std::optional<QDateTime> expires_at;
        std::optional<QString> status; // "active" / "expired" / "cancelled"
        std::optional<int> view_count;
        std::optional<int> download_count;
        std::optional<QString> primary_item_name;
        std::optional<int> item_count;
        std::optional<QVector<DriveItem>> items;
        std::optional<QDateTime> updated_at;

        static auto FromJson(const QJsonObject& json) -> ShareItem;
        auto ToJson() const -> QJsonObject;
    };

    class ShareListModel : public QAbstractListModel {
        Q_OBJECT

    public:
        enum Roles {
            ShareIdRole = Qt::UserRole + 1,
            ShareLinkRole,
            PermissionRole,
            HasPasswordRole,
            CreatedAtRole,
            ExpiresAtRole,
            StatusRole,
            ViewCountRole,
            DownloadCountRole,
            PrimaryItemNameRole,
            ItemCountRole,
            UpdatedAtRole,
        };

        explicit ShareListModel(QObject* parent = nullptr);

        auto rowCount(const QModelIndex& parent = {}) const -> int override;
        auto data(const QModelIndex& index, int role) const -> QVariant override;
        auto roleNames() const -> QHash<int, QByteArray> override;

        auto AddItem(const ShareItem& item) -> void;
        auto RemoveItem(const QString& share_id) -> bool;
        auto Clear() -> void;
        auto GetItem(int row) const -> std::optional<ShareItem>;
        auto SetItems(const QVector<ShareItem>& items) -> void;

        Q_INVOKABLE int indexOf(const QString& share_id) const;

    private:
        QVector<ShareItem> m_items;
    };

} // namespace disk::desktop
