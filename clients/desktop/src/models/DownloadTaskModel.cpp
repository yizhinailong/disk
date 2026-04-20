/**
 * @file DownloadTaskModel.cpp
 * @brief DownloadTaskModel implementation
 *
 * @copyright Copyright (c) 2026
 */

#include "models/DownloadTaskModel.hpp"

#include <QJsonObject>

namespace disk::desktop {

    auto DownloadTask::FromJson(const QJsonObject& json) -> DownloadTask {
        DownloadTask task;
        task.task_id = json.value("task_id").toString();
        task.auth_domain = json.value("auth_domain").toString("owner");

        if (json.contains("share_id")) {
            task.share_id = json.value("share_id").toString();
        }

        task.file_id = static_cast<quint64>(json.value("file_id").toDouble(0));
        task.filename = json.value("filename").toString();
        task.file_size = static_cast<quint64>(json.value("file_size").toDouble(0));

        if (json.contains("file_hash")) {
            task.file_hash = json.value("file_hash").toString();
        }

        task.mime_type = json.value("mime_type").toString();
        task.supports_range = json.value("supports_range").toBool(false);
        task.transfer_mode = json.value("transfer_mode").toString("full");

        if (json.contains("range_start")) {
            task.range_start = static_cast<quint64>(json.value("range_start").toDouble(0));
        }
        if (json.contains("range_end")) {
            task.range_end = static_cast<quint64>(json.value("range_end").toDouble(0));
        }

        task.target_path = json.value("target_path").toString();
        task.received_bytes = static_cast<quint64>(json.value("received_bytes").toDouble(0));
        task.status = json.value("status").toString("queued");

        return task;
    }

    auto DownloadTask::ToJson() const -> QJsonObject {
        QJsonObject json;
        json["task_id"] = task_id;
        json["auth_domain"] = auth_domain;

        if (share_id.has_value()) {
            json["share_id"] = *share_id;
        }

        json["file_id"] = static_cast<double>(file_id);
        json["filename"] = filename;
        json["file_size"] = static_cast<double>(file_size);

        if (file_hash.has_value()) {
            json["file_hash"] = *file_hash;
        }

        json["mime_type"] = mime_type;
        json["supports_range"] = supports_range;
        json["transfer_mode"] = transfer_mode;

        if (range_start.has_value()) {
            json["range_start"] = static_cast<double>(*range_start);
        }
        if (range_end.has_value()) {
            json["range_end"] = static_cast<double>(*range_end);
        }

        json["target_path"] = target_path;
        json["received_bytes"] = static_cast<double>(received_bytes);
        json["status"] = status;

        return json;
    }

    DownloadTaskModel::DownloadTaskModel(QObject* parent)
        : QAbstractListModel(parent) {}

    auto DownloadTaskModel::rowCount(const QModelIndex& parent) const -> int {
        if (parent.isValid()) {
            return 0;
        }
        return m_tasks.size();
    }

    auto DownloadTaskModel::data(const QModelIndex& index, int role) const -> QVariant {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_tasks.size()) {
            return {};
        }

        const auto& task = m_tasks.at(index.row());

        switch (role) {
            case TaskIdRole       : return task.task_id;
            case AuthDomainRole   : return task.auth_domain;
            case ShareIdRole      : return task.share_id.has_value() ? QVariant(*task.share_id) : QVariant();
            case FileIdRole       : return task.file_id;
            case FilenameRole     : return task.filename;
            case FileSizeRole     : return task.file_size;
            case FileHashRole     : return task.file_hash.has_value() ? QVariant(*task.file_hash) : QVariant();
            case MimeTypeRole     : return task.mime_type;
            case SupportsRangeRole: return task.supports_range;
            case TransferModeRole : return task.transfer_mode;
            case RangeStartRole   : return task.range_start.has_value() ? QVariant(*task.range_start) : QVariant();
            case RangeEndRole     : return task.range_end.has_value() ? QVariant(*task.range_end) : QVariant();
            case TargetPathRole   : return task.target_path;
            case ReceivedBytesRole: return task.received_bytes;
            case StatusRole       : return task.status;
            case ErrorRole        : {
                if (!task.error.has_value()) {
                    return QVariant();
                }
                QJsonObject err;
                err["code"] = task.error->code;
                err["message"] = task.error->message;
                err["category"] = task.error->category;
                return err;
            }
            default: return {};
        }
    }

    auto DownloadTaskModel::roleNames() const -> QHash<int, QByteArray> {
        return {
            {        TaskIdRole,        "taskId" },
            {    AuthDomainRole,    "authDomain" },
            {       ShareIdRole,       "shareId" },
            {        FileIdRole,        "fileId" },
            {      FilenameRole,      "filename" },
            {      FileSizeRole,      "fileSize" },
            {      FileHashRole,      "fileHash" },
            {      MimeTypeRole,      "mimeType" },
            { SupportsRangeRole, "supportsRange" },
            {  TransferModeRole,  "transferMode" },
            {    RangeStartRole,    "rangeStart" },
            {      RangeEndRole,      "rangeEnd" },
            {    TargetPathRole,    "targetPath" },
            { ReceivedBytesRole, "receivedBytes" },
            {        StatusRole,        "status" },
            {         ErrorRole,         "error" },
        };
    }

    auto DownloadTaskModel::AddTask(const DownloadTask& task) -> void {
        beginInsertRows({}, m_tasks.size(), m_tasks.size());
        m_tasks.append(task);
        endInsertRows();
    }

    auto DownloadTaskModel::RemoveTask(const QString& task_id) -> bool {
        for (int i = 0; i < m_tasks.size(); ++i) {
            if (m_tasks[i].task_id == task_id) {
                beginRemoveRows({}, i, i);
                m_tasks.removeAt(i);
                endRemoveRows();
                return true;
            }
        }
        return false;
    }

    auto DownloadTaskModel::UpdateTask(const QString& task_id, const DownloadTask& updated) -> bool {
        for (int i = 0; i < m_tasks.size(); ++i) {
            if (m_tasks[i].task_id == task_id) {
                m_tasks[i] = updated;
                emit dataChanged(index(i), index(i));
                return true;
            }
        }
        return false;
    }

    auto DownloadTaskModel::Clear() -> void {
        if (m_tasks.isEmpty()) {
            return;
        }
        beginResetModel();
        m_tasks.clear();
        endResetModel();
    }

    auto DownloadTaskModel::GetTask(int row) const -> std::optional<DownloadTask> {
        if (row < 0 || row >= m_tasks.size()) {
            return std::nullopt;
        }
        return m_tasks.at(row);
    }

    auto DownloadTaskModel::FindTask(const QString& task_id) const -> int {
        for (int i = 0; i < m_tasks.size(); ++i) {
            if (m_tasks[i].task_id == task_id) {
                return i;
            }
        }
        return -1;
    }

    auto DownloadTaskModel::indexOf(const QString& task_id) const -> int {
        return FindTask(task_id);
    }

} // namespace disk::desktop
