#include "models/OperationLogListModel.hpp"

#include <QJsonObject>

namespace disk::desktop {

    auto OperationLogItem::FromJson(const QJsonObject& json) -> OperationLogItem {
        OperationLogItem item;
        item.id = json.value("id").toVariant().toULongLong();
        item.action = json.value("action").toString();
        item.target_type = json.value("target_type").toString();
        item.target_id = json.value("target_id").toVariant().toULongLong();
        item.target_name = json.value("target_name").toString();
        item.details = json.value("details").toString();
        item.ip_address = json.value("ip_address").toString();
        item.created_at = json.value("created_at").toString();
        return item;
    }

    OperationLogListModel::OperationLogListModel(QObject* parent)
        : QAbstractListModel(parent) {}

    auto OperationLogListModel::rowCount(const QModelIndex& parent) const -> int {
        if (parent.isValid()) {
            return 0;
        }
        return m_items.size();
    }

    auto OperationLogListModel::data(const QModelIndex& index, int role) const -> QVariant {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
            return {};
        }

        const auto& item = m_items.at(index.row());
        switch (role) {
            case IdRole        : return item.id;
            case ActionRole    : return item.action;
            case TargetTypeRole: return item.target_type;
            case TargetIdRole  : return item.target_id;
            case TargetNameRole: return item.target_name;
            case DetailsRole   : return item.details;
            case IpAddressRole : return item.ip_address;
            case CreatedAtRole : return item.created_at;
            default            : return {};
        }
    }

    auto OperationLogListModel::roleNames() const -> QHash<int, QByteArray> {
        return {
            { IdRole,         "id" },
            { ActionRole,     "action" },
            { TargetTypeRole, "targetType" },
            { TargetIdRole,   "targetId" },
            { TargetNameRole, "targetName" },
            { DetailsRole,    "details" },
            { IpAddressRole,  "ipAddress" },
            { CreatedAtRole,  "createdAt" },
        };
    }

    auto OperationLogListModel::SetItems(const QVector<OperationLogItem>& items) -> void {
        beginResetModel();
        m_items = items;
        endResetModel();
    }

    auto OperationLogListModel::GetItem(int row) -> std::optional<OperationLogItem> {
        if (row < 0 || row >= m_items.size()) {
            return std::nullopt;
        }
        return m_items.at(row);
    }

    auto OperationLogListModel::Clear() -> void {
        if (m_items.isEmpty()) {
            return;
        }
        beginResetModel();
        m_items.clear();
        endResetModel();
    }

} // namespace disk::desktop
