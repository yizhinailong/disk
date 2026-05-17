/**
 * @file DriveListModel.hpp
 * @brief QAbstractListModel for mixed file/folder lists per doc 02 §3.5
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <QAbstractListModel>
#include <QVariantMap>
#include <QVector>

#include "models/DriveItem.hpp"

namespace disk::desktop {

    class DriveListModel : public QAbstractListModel {
        Q_OBJECT

    public:
        enum Roles {
            IdRole = Qt::UserRole + 1,
            KindRole,
            NameRole,
            SizeRole,
            MimeTypeRole,
            HashRole,
            ItemCountRole,
            ParentIdRole,
            PathRole,
            CreatedAtRole,
            UpdatedAtRole,
            OriginRole
        };

        explicit DriveListModel(QObject* parent = nullptr);

        auto rowCount(const QModelIndex& parent = {}) const -> int override;
        auto data(const QModelIndex& index, int role) const -> QVariant override;
        auto roleNames() const -> QHash<int, QByteArray> override;

        auto AddItem(const DriveItem& item) -> void;
        auto AddItems(const QVector<DriveItem>& items) -> void;
        auto RemoveItem(quint64 id) -> bool;
        auto Clear() -> void;
        auto SetItems(const QVector<DriveItem>& items) -> void;
        auto indexOf(quint64 id) const -> int;
        auto GetItem(int row) const -> std::optional<DriveItem>;
        Q_INVOKABLE QVariantMap GetItemById(const QString& id) const;

    private:
        QVector<DriveItem> m_items;
    };

} // namespace disk::desktop
