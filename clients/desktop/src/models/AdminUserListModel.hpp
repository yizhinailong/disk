/**
 * @file AdminUserListModel.hpp
 * @brief QAbstractListModel for admin user list data
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <QAbstractListModel>
#include <QVector>
#include <QString>

namespace disk::desktop {

    struct AdminUserItem {
        quint64 id{ 0 };
        QString username;
        QString email;
        QString nickname;
        int role{ 0 };       // 0 = user, 1 = admin
        int status{ 1 };     // 0 = disabled, 1 = normal, 2 = locked
        qint64 storage_quota{ 0 };
        qint64 storage_used{ 0 };
        qint64 storage_reserved{ 0 };
        QString created_at;
        QString last_login_at;

        static auto FromJson(const QJsonObject& json) -> AdminUserItem;
    };

    class AdminUserListModel : public QAbstractListModel {
        Q_OBJECT

    public:
        enum Roles {
            IdRole = Qt::UserRole + 1,
            UsernameRole,
            EmailRole,
            NicknameRole,
            RoleRole,
            StatusRole,
            StorageQuotaRole,
            StorageUsedRole,
            StorageReservedRole,
            CreatedAtRole,
            LastLoginAtRole,
        };

        explicit AdminUserListModel(QObject* parent = nullptr);

        auto rowCount(const QModelIndex& parent = {}) const -> int override;
        auto data(const QModelIndex& index, int role) const -> QVariant override;
        auto roleNames() const -> QHash<int, QByteArray> override;

        auto SetItems(const QVector<AdminUserItem>& items) -> void;
        auto GetItem(int row) -> std::optional<AdminUserItem>;
        auto Clear() -> void;

    private:
        QVector<AdminUserItem> m_items;
    };

} // namespace disk::desktop