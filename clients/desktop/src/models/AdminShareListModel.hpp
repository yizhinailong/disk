/**
 * @file AdminShareListModel.hpp
 * @brief QAbstractListModel for admin share list
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <QAbstractListModel>
#include <QVector>
#include <QString>
#include <QJsonObject>

namespace disk::desktop {

    struct AdminShareItem {
        quint64 id{ 0 };
        quint64 user_id{ 0 };
        QString username;
        quint64 file_id{ 0 };
        QString file_name;
        QString share_code;
        int status{ 0 }; // 0=cancelled, 1=active, 2=expired
        int access_count{ 0 };
        QString created_at;
        QString expires_at;

        static auto FromJson(const QJsonObject& json) -> AdminShareItem;
    };

    class AdminShareListModel : public QAbstractListModel {
        Q_OBJECT

    public:
        enum Roles {
            IdRole = Qt::UserRole + 1,
            UserIdRole,
            UsernameRole,
            FileIdRole,
            FileNameRole,
            ShareCodeRole,
            StatusRole,
            AccessCountRole,
            CreatedAtRole,
            ExpiresAtRole,
        };

        explicit AdminShareListModel(QObject* parent = nullptr);

        auto rowCount(const QModelIndex& parent = {}) const -> int override;
        auto data(const QModelIndex& index, int role) const -> QVariant override;
        auto roleNames() const -> QHash<int, QByteArray> override;

        auto SetItems(const QVector<AdminShareItem>& items) -> void;
        auto GetItem(int row) -> std::optional<AdminShareItem>;
        auto Clear() -> void;

    private:
        QVector<AdminShareItem> m_items;
    };

} // namespace disk::desktop