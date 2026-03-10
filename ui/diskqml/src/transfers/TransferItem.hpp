/**
 * @file TransferItem.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Data struct representing a single upload/download transfer
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Pure value type — no QObject, no Q_PROPERTY.
 * Stored by value in QVector inside TransferQueueModel.
 * Serialisable to/from JSON for persistence by TransferStore.
 */

#pragma once

#include <QJsonObject>
#include <QString>
#include <QUuid>

namespace disk::qml::transfers {

    /**
     * @brief Direction of the transfer (upload or download).
     */
    enum class TransferDirection {
        Upload,
        Download,
    };

    /**
     * @brief Lifecycle status of a transfer.
     */
    enum class TransferStatus {
        Queued,    ///< Waiting for a concurrency slot
        Running,   ///< Actively transferring
        Paused,    ///< Paused by user
        Completed, ///< Finished successfully
        Failed,    ///< Encountered an error
    };

    /**
     * @brief Data struct for a single transfer entry.
     *
     * @details
     * Maps to one row in the TransferQueueModel.
     * The `id` is a UUID string generated at creation time.
     */
    struct TransferItem {
        QString id; ///< Unique identifier (UUID)
        TransferDirection direction{ TransferDirection::Upload };
        QString fileName;
        qint64 totalBytes{ 0 };
        qint64 doneBytes{ 0 };
        TransferStatus status{ TransferStatus::Queued };
        qint64 speed{ 0 }; ///< Bytes per second
        qint64 eta{ 0 };   ///< Estimated seconds remaining
        QString error;     ///< Error message (empty when no error)

        // ----- Derived helpers -----

        /**
         * @brief Progress percentage in [0, 100].
         */
        [[nodiscard]] auto Progress() const -> int {
            if (totalBytes <= 0) {
                return 0;
            }
            return static_cast<int>(doneBytes * 100 / totalBytes);
        }

        /**
         * @brief Factory — create a new item with a fresh UUID.
         */
        static auto Create(TransferDirection dir, const QString& fileName, qint64 totalBytes) -> TransferItem {
            return TransferItem{
                .id = QUuid::createUuid().toString(QUuid::WithoutBraces),
                .direction = dir,
                .fileName = fileName,
                .totalBytes = totalBytes,
            };
        }

        // ----- JSON serialisation -----

        [[nodiscard]] auto ToJson() const -> QJsonObject {
            return QJsonObject{
                {         "id",                          id },
                {  "direction", static_cast<int>(direction) },
                {   "fileName",                    fileName },
                { "totalBytes",                  totalBytes },
                {  "doneBytes",                   doneBytes },
                {     "status",    static_cast<int>(status) },
                {      "error",                       error },
            };
        }

        static auto FromJson(const QJsonObject& obj) -> TransferItem {
            TransferItem item;
            item.id = obj.value("id").toString();
            item.direction = static_cast<TransferDirection>(obj.value("direction").toInt(0));
            item.fileName = obj.value("fileName").toString();
            item.totalBytes = static_cast<qint64>(obj.value("totalBytes").toDouble(0));
            item.doneBytes = static_cast<qint64>(obj.value("doneBytes").toDouble(0));
            item.status = TransferStatus::Paused; // Always rehydrate as Paused
            item.speed = 0;
            item.eta = 0;
            item.error = obj.value("error").toString();
            return item;
        }
    };

} // namespace disk::qml::transfers
