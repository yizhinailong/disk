/**
 * @file TrashListModel.hpp
 * @brief QAbstractListModel for trash items per doc 02 §3.10
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QVector>

namespace disk::desktop {

    struct TrashItem {
        quint64 trash_id{ 0 };
        QString kind; // "file" / "folder"
        quint64 original_id{ 0 };
        QString name;
        quint64 size{ 0 };
        QString original_path;
        QDateTime deleted_at;
        QDateTime expires_at;

        static auto FromJson(const QJsonObject& json) -> TrashItem;
        auto ToJson() const -> QJsonObject;
    };

    class TrashListModel : public QAbstractListModel {
        Q_OBJECT

    public:
        enum Roles {
            TrashIdRole = Qt::UserRole + 1,
            KindRole,
            OriginalIdRole,
            NameRole,
            SizeRole,
            OriginalPathRole,
            DeletedAtRole,
            ExpiresAtRole,
        };

        explicit TrashListModel(QObject* parent = nullptr);

        auto rowCount(const QModelIndex& parent = {}) const -> int override;
        auto data(const QModelIndex& index, int role) const -> QVariant override;
        auto roleNames() const -> QHash<int, QByteArray> override;

        auto AddItem(const TrashItem& item) -> void;
        auto RemoveItem(quint64 trash_id) -> bool;
        auto Clear() -> void;
        auto GetItem(int row) const -> std::optional<TrashItem>;
        auto SetItems(const QVector<TrashItem>& items) -> void;

        Q_INVOKABLE int indexOf(quint64 trash_id) const;

    private:
        QVector<TrashItem> m_items;
    };

} // namespace disk::desktop
