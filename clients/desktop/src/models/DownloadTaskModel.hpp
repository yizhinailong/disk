/**
 * @file DownloadTaskModel.hpp
 * @brief QAbstractListModel for download tasks per doc 02 §3.8
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QVector>

#include "network/ErrorAdapter.hpp"

namespace disk::desktop {

    struct DownloadTask {
        QString task_id;
        QString auth_domain; // "owner" / "visitor"
        std::optional<QString> share_id;
        quint64 file_id{ 0 };
        QString filename;
        quint64 file_size{ 0 };
        std::optional<QString> file_hash;
        QString mime_type;
        bool supports_range{ false };
        QString transfer_mode; // "full" / "range"
        std::optional<quint64> range_start;
        std::optional<quint64> range_end;
        QString target_path;
        quint64 received_bytes{ 0 };
        QString status; // queued / downloading / paused / completed / failed / cancelled
        std::optional<ApiError> error;

        static auto FromJson(const QJsonObject& json) -> DownloadTask;
        auto ToJson() const -> QJsonObject;
    };

    class DownloadTaskModel : public QAbstractListModel {
        Q_OBJECT

    public:
        enum Roles {
            TaskIdRole = Qt::UserRole + 1,
            AuthDomainRole,
            ShareIdRole,
            FileIdRole,
            FilenameRole,
            FileSizeRole,
            FileHashRole,
            MimeTypeRole,
            SupportsRangeRole,
            TransferModeRole,
            RangeStartRole,
            RangeEndRole,
            TargetPathRole,
            ReceivedBytesRole,
            StatusRole,
            ErrorRole,
        };

        explicit DownloadTaskModel(QObject* parent = nullptr);

        auto rowCount(const QModelIndex& parent = {}) const -> int override;
        auto data(const QModelIndex& index, int role) const -> QVariant override;
        auto roleNames() const -> QHash<int, QByteArray> override;

        auto AddTask(const DownloadTask& task) -> void;
        auto RemoveTask(const QString& task_id) -> bool;
        auto UpdateTask(const QString& task_id, const DownloadTask& updated) -> bool;
        auto Clear() -> void;
        auto GetTask(int row) const -> std::optional<DownloadTask>;
        auto FindTask(const QString& task_id) const -> int;

        Q_INVOKABLE int indexOf(const QString& task_id) const;

    private:
        QVector<DownloadTask> m_tasks;
    };

} // namespace disk::desktop
