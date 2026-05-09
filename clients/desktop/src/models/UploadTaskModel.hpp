/**
 * @file UploadTaskModel.hpp
 * @brief QAbstractListModel for upload tasks per doc 02 §3.7
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QVector>

#include "network/ErrorAdapter.hpp"

namespace disk::desktop {

    struct UploadTask {
        QString task_id;
        QString local_path;
        QString filename;
        quint64 file_size{ 0 };
        QString file_hash;
        quint64 parent_id{ 0 };
        std::optional<QString> upload_id;
        std::optional<int> chunk_size;
        std::optional<int> total_chunks;
        QVector<int> uploaded_chunk_indices;
        bool instant_upload{ false };
        QString status; // queued / hashing / initializing / uploading / completed / failed / cancelled
        std::optional<ApiError> error;

        static auto FromJson(const QJsonObject& json) -> UploadTask;
        auto ToJson() const -> QJsonObject;
    };

    class UploadTaskModel : public QAbstractListModel {
        Q_OBJECT

    public:
        enum Roles {
            TaskIdRole = Qt::UserRole + 1,
            LocalPathRole,
            FilenameRole,
            FileSizeRole,
            FileHashRole,
            ParentIdRole,
            UploadIdRole,
            ChunkSizeRole,
            TotalChunksRole,
            UploadedChunkIndicesRole,
            InstantUploadRole,
            StatusRole,
            ErrorRole,
        };

        explicit UploadTaskModel(QObject* parent = nullptr);

        auto rowCount(const QModelIndex& parent = {}) const -> int override;
        auto data(const QModelIndex& index, int role) const -> QVariant override;
        auto roleNames() const -> QHash<int, QByteArray> override;

        auto AddTask(const UploadTask& task) -> void;
        auto RemoveTask(const QString& task_id) -> bool;
        auto UpdateTask(const QString& task_id, const UploadTask& updated) -> bool;
        auto Clear() -> void;
        auto GetTask(int row) const -> std::optional<UploadTask>;
        auto FindTask(const QString& task_id) const -> int;

    private:
        QVector<UploadTask> m_tasks;
    };

} // namespace disk::desktop
