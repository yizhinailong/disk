/**
 * @file TransferQueueModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QAbstractListModel for upload/download transfer queues
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Pure data model — no business logic, no API calls.
 * A ViewModel or TransferManager populates this model via
 * AddTransfer / UpdateProgress / RemoveTransfer.
 *
 * Roles are aligned to the TransferItem struct and the
 * transfer-panel design spec (docs/ui/design/transfer-panel.md).
 */

#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVector>

#include <QtQml/qqmlregistration.h>

#include <transfers/TransferItem.hpp>

namespace disk::qml::transfers {

    /**
     * @brief QAbstractListModel exposing transfer items to QML ListView.
     *
     * @details
     * Provides the following roles for QML delegates:
     *   - transferId, direction, fileName, totalBytes, doneBytes,
     *     status, progress, speed, eta, error
     *
     * Populate via AddTransfer(). Update progress via UpdateProgress().
     * The model does NOT manage actual file I/O; a TransferManager
     * is responsible for driving transfers and calling update methods.
     */
    class TransferQueueModel : public QAbstractListModel {
        Q_OBJECT
        QML_ELEMENT

        /// Number of items currently in the model (convenience for QML).
        Q_PROPERTY(int count READ Count NOTIFY countChanged)

    public:
        /**
         * @brief Custom data roles exposed to QML via roleNames().
         */
        enum Roles {
            TransferIdRole = Qt::UserRole + 100,
            DirectionRole,
            FileNameRole,
            TotalBytesRole,
            DoneBytesRole,
            StatusRole,
            ProgressRole,
            SpeedRole,
            EtaRole,
            ErrorRole,
        };
        Q_ENUM(Roles)

        explicit TransferQueueModel(QObject* parent = nullptr);
        ~TransferQueueModel() override = default;

        // ==================== QAbstractListModel interface ====================

        [[nodiscard]] auto rowCount(const QModelIndex& parent = QModelIndex()) const -> int override;
        [[nodiscard]] auto data(const QModelIndex& index, int role = Qt::DisplayRole) const
            -> QVariant override;
        [[nodiscard]] auto roleNames() const -> QHash<int, QByteArray> override;

        // ==================== Public API ====================

        [[nodiscard]] auto Count() const -> int;

        /**
         * @brief Append a new transfer to the queue.
         * @param item  Transfer data. Must have a unique id.
         */
        Q_INVOKABLE void AddTransfer(const TransferItem& item);

        /**
         * @brief Remove a transfer by id.
         * @param id  Transfer UUID.
         */
        Q_INVOKABLE void RemoveTransfer(const QString& id);

        /**
         * @brief Update the progress of an existing transfer.
         */
        Q_INVOKABLE void UpdateProgress(const QString& id, qint64 doneBytes, qint64 speed, qint64 eta);

        /**
         * @brief Update the status of an existing transfer.
         */
        Q_INVOKABLE void UpdateStatus(const QString& id, TransferStatus status, const QString& error = {});

        /**
         * @brief Remove all items with status == Completed.
         */
        Q_INVOKABLE void ClearCompleted();

        /**
         * @brief Remove all items with status == Failed.
         */
        Q_INVOKABLE void ClearFailed();

        /**
         * @brief Set all Running items to Paused.
         */
        Q_INVOKABLE void PauseAll();

        /**
         * @brief Set all Paused items to Queued (ready to be picked up by engine).
         */
        Q_INVOKABLE void ResumeAll();

        /**
         * @brief Remove all items from the model.
         */
        Q_INVOKABLE void Clear();

        /**
         * @brief Retrieve the item at @p row (bounds-checked).
         * @return std::nullopt when @p row is out of range.
         */
        [[nodiscard]] auto ItemAt(int row) const -> std::optional<TransferItem>;

        /**
         * @brief Retrieve all items (for persistence).
         */
        [[nodiscard]] auto Items() const -> const QVector<TransferItem>&;

        /**
         * @brief Bulk-replace the entire model contents (used on rehydration from disk).
         */
        void ResetItems(const QVector<TransferItem>& items);

    signals:
        void countChanged();

    private:
        /**
         * @brief Find the row index for a given transfer id.
         * @return -1 if not found.
         */
        [[nodiscard]] auto FindRow(const QString& id) const -> int;

        /**
         * @brief Emit dataChanged for all roles on a single row.
         */
        void EmitRowChanged(int row);

        /**
         * @brief Remove items matching a given status.
         */
        void RemoveByStatus(TransferStatus status);

        QVector<TransferItem> m_items;
        QHash<QString, int> m_id_index; ///< id → row for O(1) lookups
    };

} // namespace disk::qml::transfers
