#pragma once

#include <QAbstractListModel>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <optional>

namespace disk::desktop {

    struct OperationLogItem {
        quint64 id{ 0 };
        QString action;
        QString target_type;
        quint64 target_id{ 0 };
        QString target_name;
        QString details;
        QString ip_address;
        QString created_at;

        static auto FromJson(const QJsonObject& json) -> OperationLogItem;
    };

    class OperationLogListModel : public QAbstractListModel {
        Q_OBJECT

    public:
        enum Roles {
            IdRole = Qt::UserRole + 1,
            ActionRole,
            TargetTypeRole,
            TargetIdRole,
            TargetNameRole,
            DetailsRole,
            IpAddressRole,
            CreatedAtRole,
        };

        explicit OperationLogListModel(QObject* parent = nullptr);

        auto rowCount(const QModelIndex& parent = {}) const -> int override;
        auto data(const QModelIndex& index, int role) const -> QVariant override;
        auto roleNames() const -> QHash<int, QByteArray> override;

        auto SetItems(const QVector<OperationLogItem>& items) -> void;
        auto GetItem(int row) -> std::optional<OperationLogItem>;
        auto Clear() -> void;

    private:
        QVector<OperationLogItem> m_items;
    };

} // namespace disk::desktop
