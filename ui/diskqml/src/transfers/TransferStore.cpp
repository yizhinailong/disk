/**
 * @file TransferStore.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief TransferStore implementation — JSON persistence
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 */

#include "transfers/TransferStore.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace disk::qml::transfers {

    static constexpr auto kFileName = "transfers.json";

    TransferStore::TransferStore(QObject* parent)
        : QObject(parent) {
        const auto dataDir =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dataDir);
        m_file_path = dataDir + QStringLiteral("/") + QLatin1String(kFileName);
    }

    auto TransferStore::Save(const QVector<TransferItem>& uploads, const QVector<TransferItem>& downloads) -> bool {
        QJsonArray uploadsArr;
        for (const auto& item : uploads) {
            if (item.status != TransferStatus::Completed) {
                uploadsArr.append(item.ToJson());
            }
        }

        QJsonArray downloadsArr;
        for (const auto& item : downloads) {
            if (item.status != TransferStatus::Completed) {
                downloadsArr.append(item.ToJson());
            }
        }

        QJsonObject root;
        root["uploads"] = uploadsArr;
        root["downloads"] = downloadsArr;

        QFile file(m_file_path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }

        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        return true;
    }

    auto TransferStore::Load(QVector<TransferItem>& uploads, QVector<TransferItem>& downloads) -> bool {
        QFile file(m_file_path);
        if (!file.exists()) {
            return true; // No persisted state is a valid (empty) state
        }
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }

        const auto doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject()) {
            return false;
        }

        const auto root = doc.object();

        const auto uploadsArr = root.value("uploads").toArray();
        uploads.reserve(uploadsArr.size());
        for (const auto& val : uploadsArr) {
            uploads.append(TransferItem::FromJson(val.toObject()));
        }

        const auto downloadsArr = root.value("downloads").toArray();
        downloads.reserve(downloadsArr.size());
        for (const auto& val : downloadsArr) {
            downloads.append(TransferItem::FromJson(val.toObject()));
        }

        return true;
    }

    auto TransferStore::FilePath() const noexcept -> const QString& {
        return m_file_path;
    }

} // namespace disk::qml::transfers
