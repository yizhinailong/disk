/**
 * @file UploadTaskModel.cpp
 * @brief UploadTaskModel implementation
 *
 * @copyright Copyright (c) 2026
 */

#include "models/UploadTaskModel.hpp"

#include <QJsonArray>
#include <QJsonObject>

namespace disk::desktop {

    auto UploadTask::FromJson(const QJsonObject& json) -> UploadTask {
        UploadTask task;
        task.task_id = json.value("task_id").toString();
        task.local_path = json.value("local_path").toString();
        task.filename = json.value("filename").toString();
        task.file_size = static_cast<quint64>(json.value("file_size").toDouble(0));
        task.file_hash = json.value("file_hash").toString();
        task.parent_id = static_cast<quint64>(json.value("parent_id").toDouble(0));

        if (json.contains("upload_id")) {
            task.upload_id = json.value("upload_id").toString();
        }
        if (json.contains("chunk_size")) {
            task.chunk_size = json.value("chunk_size").toInt();
        }
        if (json.contains("total_chunks")) {
            task.total_chunks = json.value("total_chunks").toInt();
        }

        if (json.contains("uploaded_chunk_indices") && json["uploaded_chunk_indices"].isArray()) {
            for (const auto& v : json["uploaded_chunk_indices"].toArray()) {
                task.uploaded_chunk_indices.append(v.toInt());
            }
        }

        task.instant_upload = json.value("instant_upload").toBool(false);
        task.status = json.value("status").toString("queued");

        return task;
    }

    auto UploadTask::ToJson() const -> QJsonObject {
        QJsonObject json;
        json["task_id"] = task_id;
        json["local_path"] = local_path;
        json["filename"] = filename;
        json["file_size"] = static_cast<double>(file_size);
        json["file_hash"] = file_hash;
        json["parent_id"] = static_cast<double>(parent_id);

        if (upload_id.has_value()) {
            json["upload_id"] = *upload_id;
        }
        if (chunk_size.has_value()) {
            json["chunk_size"] = *chunk_size;
        }
        if (total_chunks.has_value()) {
            json["total_chunks"] = *total_chunks;
        }

        QJsonArray indices;
        for (int idx : uploaded_chunk_indices) {
            indices.append(idx);
        }
        json["uploaded_chunk_indices"] = indices;

        json["instant_upload"] = instant_upload;
        json["status"] = status;

        return json;
    }

    UploadTaskModel::UploadTaskModel(QObject* parent)
        : QAbstractListModel(parent) {}

    auto UploadTaskModel::rowCount(const QModelIndex& parent) const -> int {
        if (parent.isValid()) {
            return 0;
        }
        return m_tasks.size();
    }

    auto UploadTaskModel::data(const QModelIndex& index, int role) const -> QVariant {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_tasks.size()) {
            return {};
        }

        const auto& task = m_tasks.at(index.row());

        switch (role) {
            case TaskIdRole              : return task.task_id;
            case LocalPathRole           : return task.local_path;
            case FilenameRole            : return task.filename;
            case FileSizeRole            : return task.file_size;
            case FileHashRole            : return task.file_hash;
            case ParentIdRole            : return task.parent_id;
            case UploadIdRole            : return task.upload_id.has_value() ? QVariant(*task.upload_id) : QVariant();
            case ChunkSizeRole           : return task.chunk_size.has_value() ? QVariant(*task.chunk_size) : QVariant();
            case TotalChunksRole         : return task.total_chunks.has_value() ? QVariant(*task.total_chunks) : QVariant();
            case UploadedChunkIndicesRole: {
                QVariantList list;
                for (int idx : task.uploaded_chunk_indices) {
                    list.append(idx);
                }
                return list;
            }
            case InstantUploadRole: return task.instant_upload;
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

    auto UploadTaskModel::roleNames() const -> QHash<int, QByteArray> {
        return {
            {               TaskIdRole,               "taskId" },
            {            LocalPathRole,            "localPath" },
            {             FilenameRole,             "filename" },
            {             FileSizeRole,             "fileSize" },
            {             FileHashRole,             "fileHash" },
            {             ParentIdRole,             "parentId" },
            {             UploadIdRole,             "uploadId" },
            {            ChunkSizeRole,            "chunkSize" },
            {          TotalChunksRole,          "totalChunks" },
            { UploadedChunkIndicesRole, "uploadedChunkIndices" },
            {        InstantUploadRole,        "instantUpload" },
            {               StatusRole,               "status" },
            {                ErrorRole,                "error" },
        };
    }

    auto UploadTaskModel::AddTask(const UploadTask& task) -> void {
        beginInsertRows({}, m_tasks.size(), m_tasks.size());
        m_tasks.append(task);
        endInsertRows();
    }

    auto UploadTaskModel::RemoveTask(const QString& task_id) -> bool {
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

    auto UploadTaskModel::UpdateTask(const QString& task_id, const UploadTask& updated) -> bool {
        for (int i = 0; i < m_tasks.size(); ++i) {
            if (m_tasks[i].task_id == task_id) {
                m_tasks[i] = updated;
                emit dataChanged(index(i), index(i));
                return true;
            }
        }
        return false;
    }

    auto UploadTaskModel::Clear() -> void {
        if (m_tasks.isEmpty()) {
            return;
        }
        beginResetModel();
        m_tasks.clear();
        endResetModel();
    }

    auto UploadTaskModel::GetTask(int row) const -> std::optional<UploadTask> {
        if (row < 0 || row >= m_tasks.size()) {
            return std::nullopt;
        }
        return m_tasks.at(row);
    }

    auto UploadTaskModel::FindTask(const QString& task_id) const -> int {
        for (int i = 0; i < m_tasks.size(); ++i) {
            if (m_tasks[i].task_id == task_id) {
                return i;
            }
        }
        return -1;
    }

    auto UploadTaskModel::indexOf(const QString& task_id) const -> int {
        return FindTask(task_id);
    }

} // namespace disk::desktop
