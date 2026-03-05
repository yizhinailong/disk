/**
 * @file TransferStore.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Persist transfer queue metadata to JSON under QStandardPaths::AppDataLocation
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include <transfers/TransferItem.hpp>

namespace disk::qml::transfers {

    class TransferStore : public QObject {
        Q_OBJECT

    public:
        explicit TransferStore(QObject* parent = nullptr);
        ~TransferStore() override = default;

        TransferStore(const TransferStore&) = delete;
        auto operator=(const TransferStore&) -> TransferStore& = delete;

        auto Save(const QVector<TransferItem>& uploads, const QVector<TransferItem>& downloads) -> bool;

        auto Load(QVector<TransferItem>& uploads, QVector<TransferItem>& downloads) -> bool;

        [[nodiscard]] auto FilePath() const noexcept -> const QString&;

    private:
        QString m_file_path;
    };

} // namespace disk::qml::transfers
