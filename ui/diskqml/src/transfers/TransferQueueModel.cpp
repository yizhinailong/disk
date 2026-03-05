/**
 * @file TransferQueueModel.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QAbstractListModel implementation for transfer queues
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 */

#include "transfers/TransferQueueModel.hpp"

namespace disk::qml::transfers {

    TransferQueueModel::TransferQueueModel(QObject* parent)
        : QAbstractListModel(parent) {}

    // ==================== QAbstractListModel interface ====================

    auto TransferQueueModel::rowCount(const QModelIndex& parent) const -> int {
        if (parent.isValid()) {
            return 0;
        }
        return static_cast<int>(m_items.size());
    }

    auto TransferQueueModel::data(const QModelIndex& index, int role) const -> QVariant {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
            return {};
        }

        const auto& item = m_items.at(index.row());

        switch (role) {
            case TransferIdRole: return item.id;
            case DirectionRole : return static_cast<int>(item.direction);
            case FileNameRole  : return item.fileName;
            case TotalBytesRole: return QVariant::fromValue(item.totalBytes);
            case DoneBytesRole : return QVariant::fromValue(item.doneBytes);
            case StatusRole    : return static_cast<int>(item.status);
            case ProgressRole  : return item.Progress();
            case SpeedRole     : return QVariant::fromValue(item.speed);
            case EtaRole       : return QVariant::fromValue(item.eta);
            case ErrorRole     : return item.error;
            default            : return {};
        }
    }

    auto TransferQueueModel::roleNames() const -> QHash<int, QByteArray> {
        static const QHash<int, QByteArray> roles{
            { TransferIdRole, "transferId" },
            {  DirectionRole,  "direction" },
            {   FileNameRole,   "fileName" },
            { TotalBytesRole, "totalBytes" },
            {  DoneBytesRole,  "doneBytes" },
            {     StatusRole,     "status" },
            {   ProgressRole,   "progress" },
            {      SpeedRole,      "speed" },
            {        EtaRole,        "eta" },
            {      ErrorRole,      "error" },
        };
        return roles;
    }

    // ==================== Public API ====================

    auto TransferQueueModel::Count() const -> int {
        return static_cast<int>(m_items.size());
    }

    void TransferQueueModel::AddTransfer(const TransferItem& item) {
        const int row = static_cast<int>(m_items.size());
        beginInsertRows(QModelIndex(), row, row);
        m_items.append(item);
        m_id_index.insert(item.id, row);
        endInsertRows();
        emit countChanged();
    }

    void TransferQueueModel::RemoveTransfer(const QString& id) {
        const int row = FindRow(id);
        if (row < 0) {
            return;
        }

        beginRemoveRows(QModelIndex(), row, row);
        m_items.removeAt(row);
        m_id_index.remove(id);

        // Rebuild index for items after the removed row
        for (int i = row; i < m_items.size(); ++i) {
            m_id_index[m_items.at(i).id] = i;
        }
        endRemoveRows();
        emit countChanged();
    }

    void TransferQueueModel::UpdateProgress(const QString& id, qint64 doneBytes, qint64 speed, qint64 eta) {
        const int row = FindRow(id);
        if (row < 0) {
            return;
        }

        auto& item = m_items[row];
        item.doneBytes = doneBytes;
        item.speed = speed;
        item.eta = eta;
        EmitRowChanged(row);
    }

    void TransferQueueModel::UpdateStatus(const QString& id, TransferStatus status, const QString& error) {
        const int row = FindRow(id);
        if (row < 0) {
            return;
        }

        auto& item = m_items[row];
        item.status = status;
        item.error = error;

        if (status == TransferStatus::Paused || status == TransferStatus::Failed) {
            item.speed = 0;
            item.eta = 0;
        }
        EmitRowChanged(row);
    }

    void TransferQueueModel::ClearCompleted() {
        RemoveByStatus(TransferStatus::Completed);
    }

    void TransferQueueModel::ClearFailed() {
        RemoveByStatus(TransferStatus::Failed);
    }

    void TransferQueueModel::PauseAll() {
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items[i].status == TransferStatus::Running) {
                m_items[i].status = TransferStatus::Paused;
                m_items[i].speed = 0;
                m_items[i].eta = 0;
                EmitRowChanged(i);
            }
        }
    }

    void TransferQueueModel::ResumeAll() {
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items[i].status == TransferStatus::Paused) {
                m_items[i].status = TransferStatus::Queued;
                EmitRowChanged(i);
            }
        }
    }

    void TransferQueueModel::Clear() {
        if (m_items.isEmpty()) {
            return;
        }
        beginResetModel();
        m_items.clear();
        m_id_index.clear();
        endResetModel();
        emit countChanged();
    }

    auto TransferQueueModel::ItemAt(int row) const -> std::optional<TransferItem> {
        if (row < 0 || row >= m_items.size()) {
            return std::nullopt;
        }
        return m_items.at(row);
    }

    auto TransferQueueModel::Items() const -> const QVector<TransferItem>& {
        return m_items;
    }

    void TransferQueueModel::ResetItems(const QVector<TransferItem>& items) {
        beginResetModel();
        m_items = items;
        m_id_index.clear();
        for (int i = 0; i < m_items.size(); ++i) {
            m_id_index.insert(m_items.at(i).id, i);
        }
        endResetModel();
        emit countChanged();
    }

    // ==================== Private helpers ====================

    auto TransferQueueModel::FindRow(const QString& id) const -> int {
        auto it = m_id_index.constFind(id);
        if (it == m_id_index.constEnd()) {
            return -1;
        }
        return it.value();
    }

    void TransferQueueModel::EmitRowChanged(int row) {
        const auto idx = index(row);
        emit dataChanged(idx, idx);
    }

    void TransferQueueModel::RemoveByStatus(TransferStatus status) {
        // Remove from end to front to avoid index invalidation
        for (int i = m_items.size() - 1; i >= 0; --i) {
            if (m_items.at(i).status == status) {
                beginRemoveRows(QModelIndex(), i, i);
                m_id_index.remove(m_items.at(i).id);
                m_items.removeAt(i);
                endRemoveRows();
            }
        }

        // Rebuild index after removals
        m_id_index.clear();
        for (int i = 0; i < m_items.size(); ++i) {
            m_id_index.insert(m_items.at(i).id, i);
        }
        emit countChanged();
    }

} // namespace disk::qml::transfers
