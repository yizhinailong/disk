/**
 * @file TransferManager.cpp
 * @brief Upload/download task coordination implementation
 *
 * State machines follow docs/desktop/03-auth-network-and-transfers.md §6-7.
 *
 * @copyright Copyright (c) 2026
 */

#include "managers/TransferManager.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QTimer>
#include <QUrlQuery>
#include <QtConcurrent>

namespace disk::desktop::managers {

    // ── UploadState / DownloadState string helpers ──

    auto ToString(UploadState state) -> QString {
        switch (state) {
            case UploadState::Queued         : return "queued";
            case UploadState::Hashing        : return "hashing";
            case UploadState::Initializing   : return "initializing";
            case UploadState::InstantUploaded: return "completed";
            case UploadState::Resuming       : return "uploading";
            case UploadState::Uploading      : return "uploading";
            case UploadState::Completing     : return "completing";
            case UploadState::CancelPending  : return "cancelling";
            case UploadState::Completed      : return "completed";
            case UploadState::Cancelled      : return "cancelled";
            case UploadState::Expired        : return "expired";
            case UploadState::Failed         : return "failed";
        }
        return "unknown";
    }

    auto ToString(DownloadState state) -> QString {
        switch (state) {
            case DownloadState::Idle             : return "idle";
            case DownloadState::FetchingMetadata : return "fetching_metadata";
            case DownloadState::Ready            : return "ready";
            case DownloadState::TransferringFull : return "downloading";
            case DownloadState::TransferringRange: return "downloading";
            case DownloadState::Paused           : return "paused";
            case DownloadState::RetryWaiting     : return "retry_waiting";
            case DownloadState::Completed        : return "completed";
            case DownloadState::Cancelled        : return "cancelled";
            case DownloadState::Failed           : return "failed";
        }
        return "unknown";
    }

    // ── Constructor / Destructor ──

    TransferManager::TransferManager(
        NetworkClient* network_client,
        RequestFactory* request_factory,
        QObject* parent
    )
        : QObject(parent), m_network_client(network_client), m_request_factory(request_factory), m_upload_model(new UploadTaskModel(this)), m_download_model(new DownloadTaskModel(this)) {}

    TransferManager::~TransferManager() {
        for (auto it = m_active_uploads.begin(); it != m_active_uploads.end(); ++it) {
            if (it->reply) {
                it->reply->abort();
            }
        }
        for (auto it = m_active_downloads.begin(); it != m_active_downloads.end(); ++it) {
            if (it->reply) {
                it->reply->abort();
            }
            if (it->file) {
                it->file->close();
                delete it->file;
            }
        }
        for (auto* watcher : m_hash_watchers) {
            watcher->cancel();
            watcher->deleteLater();
        }
        m_hash_watchers.clear();
    }

    auto TransferManager::GetUploadModel() -> UploadTaskModel* {
        return m_upload_model;
    }

    auto TransferManager::GetDownloadModel() -> DownloadTaskModel* {
        return m_download_model;
    }

    // ── Upload Operations ──

    void TransferManager::StartUpload(const QString& local_path, quint64 parent_id) {
        auto task_id = CreateUploadTask(local_path, parent_id);
        if (!task_id.isEmpty()) {
            StartHashing(task_id);
        }
    }

    void TransferManager::RetryUpload(const QString& task_id) {
        int row = m_upload_model->FindTask(task_id);
        if (row < 0) {
            return;
        }

        auto task = *m_upload_model->GetTask(row);
        if (task.status != "failed" && task.status != "expired") {
            return;
        }

        task.error = std::nullopt;
        task.uploaded_chunk_indices.clear();
        task.upload_id = std::nullopt;
        task.instant_upload = false;
        m_upload_model->UpdateTask(task_id, task);

        if (m_active_uploads.contains(task_id)) {
            m_active_uploads[task_id].retry_count = 0;
        }

        StartHashing(task_id);
    }

    void TransferManager::CancelUpload(const QString& task_id) {
        int row = m_upload_model->FindTask(task_id);
        if (row < 0) {
            return;
        }

        auto task = *m_upload_model->GetTask(row);

        // Per §6.2: cancel is only valid from Uploading or Completing
        if (task.status != "uploading" && task.status != "completing" &&
            task.status != "initializing" && task.status != "hashing") {
            return;
        }

        AbortActiveUpload(task_id);
        ClearHashWatcher(task_id);
        SetUploadState(task_id, UploadState::CancelPending);

        if (task.upload_id.has_value()) {
            StartUploadCancel(task_id);
        } else {
            // No server-side task yet, just mark cancelled locally
            SetUploadState(task_id, UploadState::Cancelled);
            m_active_uploads.remove(task_id);

        }
    }

    // ── Download Operations ──

    void TransferManager::StartDownload(
        quint64 file_id,
        const QString& target_path,
        const QString& auth_domain
    ) {
        auto task_id = CreateDownloadTask(file_id, target_path, auth_domain);
        if (!task_id.isEmpty()) {
            SetDownloadState(task_id, DownloadState::FetchingMetadata);
            FetchDownloadMetadata(task_id);
        }
    }

    void TransferManager::StartShareDownload(
        const QString& share_id,
        quint64 file_id,
        const QString& target_path,
        const QString& filename,
        quint64 file_size
    ) {
        auto task_id = CreateDownloadTask(
            file_id,
            target_path,
            "visitor",
            filename,
            file_size,
            share_id
        );
        if (!task_id.isEmpty()) {
            PreparePartialFileForRange(task_id);
            SetDownloadState(task_id, DownloadState::Ready);
            StartDownloadTransfer(task_id);
        }
    }

    void TransferManager::PauseDownload(const QString& task_id) {
        int row = m_download_model->FindTask(task_id);
        if (row < 0) {
            return;
        }

        auto task = *m_download_model->GetTask(row);
        if (task.status != "downloading") {
            return;
        }

        // Per §7.5: pause is purely local — abort reply, keep partial file
        AbortActiveDownload(task_id);
        SetDownloadState(task_id, DownloadState::Paused);
    }

    void TransferManager::ResumeDownload(const QString& task_id) {
        int row = m_download_model->FindTask(task_id);
        if (row < 0) {
            return;
        }

        auto task = *m_download_model->GetTask(row);
        if (task.status != "paused") {
            return;
        }

        // Per §7.3: resume goes back through FetchingMetadata
        SetDownloadState(task_id, DownloadState::FetchingMetadata);
        if (task.auth_domain == "owner") {
            FetchDownloadMetadata(task_id);
        } else {
            // Visitor: recalculate offset and go
            PreparePartialFileForRange(task_id);
            SetDownloadState(task_id, DownloadState::Ready);
            StartDownloadTransfer(task_id);
        }
    }

    void TransferManager::CancelDownload(const QString& task_id) {
        int row = m_download_model->FindTask(task_id);
        if (row < 0) {
            return;
        }

        auto task = *m_download_model->GetTask(row);

        // Per §7.5: cancel is local — abort + delete partial file
        AbortActiveDownload(task_id);

        if (!task.target_path.isEmpty()) {
            QFile::remove(task.target_path);
        }

        SetDownloadState(task_id, DownloadState::Cancelled);
        m_active_downloads.remove(task_id);
    }

    void TransferManager::RetryDownload(const QString& task_id) {
        int row = m_download_model->FindTask(task_id);
        if (row < 0) {
            return;
        }

        auto task = *m_download_model->GetTask(row);
        if (task.status != "failed") {
            return;
        }

        task.error = std::nullopt;
        m_download_model->UpdateTask(task_id, task);

        if (m_active_downloads.contains(task_id)) {
            m_active_downloads[task_id].retry_count = 0;
        }

        SetDownloadState(task_id, DownloadState::FetchingMetadata);
        if (task.auth_domain == "owner") {
            FetchDownloadMetadata(task_id);
        } else {
            PreparePartialFileForRange(task_id);
            SetDownloadState(task_id, DownloadState::Ready);
            StartDownloadTransfer(task_id);
        }
    }

    void TransferManager::ClearCompletedUploads() {
        QStringList to_remove;
        const auto count = m_upload_model->rowCount();
        for (int i = 0; i < count; ++i) {
            auto task = m_upload_model->GetTask(i);
            if (task.has_value() &&
                (task->status == "completed" || task->status == "cancelled")) {
                to_remove.append(task->task_id);
            }
        }
        for (const auto& id : to_remove) {
            m_upload_model->RemoveTask(id);
        }
    }

    void TransferManager::ClearCompletedDownloads() {
        QStringList to_remove;
        const auto count = m_download_model->rowCount();
        for (int i = 0; i < count; ++i) {
            auto task = m_download_model->GetTask(i);
            if (task.has_value() &&
                (task->status == "completed" || task->status == "cancelled")) {
                to_remove.append(task->task_id);
            }
        }
        for (const auto& id : to_remove) {
            m_download_model->RemoveTask(id);
        }
    }

    void TransferManager::ShutdownOwnerTransfers() {
        const auto is_terminal_upload = [](const QString& status) {
            return status == "completed" || status == "cancelled" ||
                   status == "expired" || status == "failed";
        };
        const auto is_terminal_download = [](const QString& status) {
            return status == "completed" || status == "cancelled" ||
                   status == "failed";
        };

        bool upload_state_changed{ false };
        QVector<QString> upload_ids;
        const auto upload_count = m_upload_model->rowCount();
        for (int i = 0; i < upload_count; ++i) {
            auto task_opt = m_upload_model->GetTask(i);
            if (!task_opt.has_value() || is_terminal_upload(task_opt->status)) {
                continue;
            }
            upload_ids.append(task_opt->task_id);
        }

        for (const auto& task_id : upload_ids) {
            AbortActiveUpload(task_id);

            const int row = m_upload_model->FindTask(task_id);
            if (row < 0) {
                continue;
            }

            auto task = *m_upload_model->GetTask(row);
            task.status = "cancelled";
            task.upload_id = std::nullopt;
            task.chunk_size = std::nullopt;
            task.total_chunks = std::nullopt;
            task.uploaded_chunk_indices.clear();
            task.error = std::nullopt;
            m_upload_model->UpdateTask(task_id, task);
            m_active_uploads.remove(task_id);
            upload_state_changed = true;
        }

        QVector<QString> owner_download_ids;
        const auto download_count = m_download_model->rowCount();
        for (int i = 0; i < download_count; ++i) {
            auto task_opt = m_download_model->GetTask(i);
            if (!task_opt.has_value() || task_opt->auth_domain != "owner" ||
                is_terminal_download(task_opt->status)) {
                continue;
            }
            owner_download_ids.append(task_opt->task_id);
        }

        for (const auto& task_id : owner_download_ids) {
            AbortActiveDownload(task_id);

            const int row = m_download_model->FindTask(task_id);
            if (row < 0) {
                continue;
            }

            auto task = *m_download_model->GetTask(row);
            if (!task.target_path.isEmpty()) {
                QFile::remove(task.target_path);
            }

            task.status = "cancelled";
            task.transfer_mode = "full";
            task.range_start = std::nullopt;
            task.range_end = std::nullopt;
            task.received_bytes = 0;
            task.error = std::nullopt;
            m_download_model->UpdateTask(task_id, task);
            m_active_downloads.remove(task_id);
        }

        if (upload_state_changed) {

        }
    }

    auto TransferManager::GetLocalReservedBytes() const -> quint64 {
        quint64 total{ 0 };
        const auto count = m_upload_model->rowCount();
        for (int i = 0; i < count; ++i) {
            auto task_opt = m_upload_model->GetTask(i);
            if (!task_opt.has_value()) {
                continue;
            }
            const auto& status = task_opt->status;
            if (status == "uploading" || status == "initializing" || status == "completing") {
                total += task_opt->file_size;
            }
        }
        return total;
    }

    // ── Upload Internal ──

    auto TransferManager::CreateUploadTask(
        const QString& local_path,
        quint64 parent_id
    ) -> QString {
        QFileInfo info(local_path);
        if (!info.exists() || !info.isFile()) {
            return {};
        }

        UploadTask task;
        task.task_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        task.local_path = local_path;
        task.filename = info.fileName();
        task.file_size = static_cast<quint64>(info.size());
        task.parent_id = parent_id;
        task.status = "queued";

        m_upload_model->AddTask(task);
        return task.task_id;
    }

    void TransferManager::StartHashing(const QString& task_id) {
        SetUploadState(task_id, UploadState::Hashing);

        int row = m_upload_model->FindTask(task_id);
        if (row < 0) {
            return;
        }
        auto task = *m_upload_model->GetTask(row);

        ClearHashWatcher(task_id);

        auto* watcher = new QFutureWatcher<QString>(this);
        m_hash_watchers[task_id] = watcher;

        const QString file_path = task.local_path;
        connect(watcher, &QFutureWatcher<QString>::finished, this, [this, task_id, watcher]() {
            const QString hash = watcher->result();

            if (m_hash_watchers.value(task_id) == watcher) {
                m_hash_watchers.remove(task_id);
            }
            watcher->deleteLater();

            int r = m_upload_model->FindTask(task_id);
            if (r < 0) {
                return;
            }
            auto t = *m_upload_model->GetTask(r);
            if (t.status != "hashing") {
                return;
            }

            if (hash.isEmpty()) {
                ApiError err;
                err.code = 0;
                err.family = "general";
                err.category = "LocalIOError";
                err.message = "计算文件哈希失败";
                err.retryable = true;
                err.action = "retry";
                FailUpload(task_id, err);
                return;
            }

            t.file_hash = hash;
            m_upload_model->UpdateTask(task_id, t);
            StartUploadInit(task_id);
        });

        watcher->setFuture(QtConcurrent::run([file_path]() {
            return TransferManager::ComputeFileMd5(file_path);
        }));
    }

    void TransferManager::StartUploadInit(const QString& task_id) {
        int row = m_upload_model->FindTask(task_id);
        if (row < 0) {
            return;
        }
        auto task = *m_upload_model->GetTask(row);

        if (task.status != "hashing" && task.status != "retrying") {
            return;
        }

        SetUploadState(task_id, UploadState::Initializing);

        task = *m_upload_model->GetTask(row);

        QJsonObject body;
        body["filename"] = task.filename;
        body["file_size"] = static_cast<qint64>(task.file_size);
        body["file_hash"] = task.file_hash;
        body["parent_id"] = static_cast<qint64>(task.parent_id);
        body["total_chunks"] = static_cast<qint64>(
            (task.file_size + kDefaultChunkSize - 1) / kDefaultChunkSize
        );

        QJsonDocument doc(body);
        auto headers = PrepareOwnerHeaders();
        headers["Content-Type"] = "application/json";

        auto reply = m_network_client->Post(
            QUrl("/api/file/upload/init"),
            doc.toJson(QJsonDocument::Compact),
            headers
        );

        ActiveUpload active;
        active.reply = reply;
        m_active_uploads[task_id] = active;

        QString tid = task_id;
        connect(reply, &QNetworkReply::finished, this, [this, tid]() {
            HandleInitResponse(tid, m_active_uploads.value(tid).reply);
        });
    }

    void TransferManager::HandleInitResponse(
        const QString& task_id,
        QNetworkReply* reply
    ) {
        if (!reply || !m_active_uploads.contains(task_id)) {
            return;
        }

        reply->deleteLater();
        m_active_uploads[task_id].reply = nullptr;

        if (reply->error() != QNetworkReply::NoError) {
            auto json_opt = ParseJsonResponse(reply);
            if (json_opt.has_value() && json_opt->contains("error")) {
                auto err = ErrorAdapter::FromJson(json_opt->value("error").toObject());
                if (err.code == 50008) {
                    ExpireUpload(task_id);
                    return;
                }
                if (err.code == 50004) {
                    err.retryable = false;
                    FailUpload(task_id, err);
                    return;
                }
                RetryOrFailUpload(task_id, err, reply);
            } else {
                ApiError err = ErrorAdapter::FromNetworkError(reply->error());
                RetryOrFailUpload(task_id, err, reply);
            }
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            ApiError err;
            err.code = 0;
            err.family = "general";
            err.category = "ParseError";
            err.message = "无效的初始化响应";
            err.retryable = true;
            err.action = "retry";
            RetryOrFailUpload(task_id, err);
            return;
        }

        auto data = json_opt->value("data").toObject();
        int row = m_upload_model->FindTask(task_id);
        if (row < 0) {
            return;
        }
        auto task = *m_upload_model->GetTask(row);

        // §6.2: instant_upload=true → InstantUploaded → Completed
        if (data.value("instant_upload").toBool(false)) {
            task.instant_upload = true;
            task.status = "completed";
            m_upload_model->UpdateTask(task_id, task);
            SetUploadState(task_id, UploadState::InstantUploaded);
            m_active_uploads.remove(task_id);

            return;
        }

        // Store init response fields
        task.upload_id = data.value("upload_id").toString();
        task.chunk_size = data.value("chunk_size").toInt(kDefaultChunkSize);
        task.total_chunks = data.value("total_chunks").toInt();

        // §6.2: resume detection — has uploaded_chunks
        if (data.contains("uploaded_chunks") &&
            data.value("uploaded_chunks").isArray()) {
            QVector<int> uploaded;
            for (const auto& v : data.value("uploaded_chunks").toArray()) {
                uploaded.append(v.toInt());
            }
            task.uploaded_chunk_indices = uploaded;

            if (!uploaded.isEmpty()) {
                SetUploadState(task_id, UploadState::Resuming);
            }
        }

        m_upload_model->UpdateTask(task_id, task);
        SetUploadState(task_id, UploadState::Uploading);
        AdvanceToNextChunk(task_id);
    }

    void TransferManager::StartChunkUpload(
        const QString& task_id,
        int chunk_index
    ) {
        int row = m_upload_model->FindTask(task_id);
        if (row < 0) {
            return;
        }
        auto task = *m_upload_model->GetTask(row);

        if (!task.upload_id.has_value() || !task.chunk_size.has_value()) {
            return;
        }

        QFile file(task.local_path);
        if (!file.open(QIODevice::ReadOnly)) {
            ApiError err;
            err.code = 0;
            err.family = "general";
            err.category = "LocalIOError";
            err.message = "无法打开文件进行读取";
            err.retryable = true;
            err.action = "retry";
            FailUpload(task_id, err);
            return;
        }

        qint64 offset = static_cast<qint64>(chunk_index) * (*task.chunk_size);
        file.seek(offset);
        qint64 remaining = static_cast<qint64>(task.file_size) - offset;
        qint64 chunk_bytes = qMin(static_cast<qint64>(*task.chunk_size), remaining);
        QByteArray chunk_data = file.read(chunk_bytes);
        file.close();

        // Compute chunk hash
        auto chunk_hash =
            QString(QCryptographicHash::hash(chunk_data, QCryptographicHash::Md5).toHex());

        auto headers = PrepareOwnerHeaders();
        headers["Content-Type"] = "application/octet-stream";

        // Backend reads upload_id, chunk_index, chunk_hash from query parameters
        // (see FileController::UploadChunk — request->getParameter(...))
        QUrl chunk_url("/api/file/upload/chunk");
        QUrlQuery query;
        query.addQueryItem("upload_id", *task.upload_id);
        query.addQueryItem("chunk_index", QString::number(chunk_index));
        query.addQueryItem("chunk_hash", chunk_hash);
        chunk_url.setQuery(query);

        auto reply = m_network_client->Post(
            chunk_url,
            chunk_data,
            headers
        );

        m_active_uploads[task_id].reply = reply;
        m_active_uploads[task_id].current_chunk = chunk_index;
        m_active_uploads[task_id].chunk_start_time = QDateTime::currentSecsSinceEpoch();

        QString tid = task_id;
        connect(reply, &QNetworkReply::finished, this, [this, tid, chunk_index]() {
            HandleChunkResponse(tid, chunk_index, m_active_uploads.value(tid).reply);
        });
    }

    void TransferManager::HandleChunkResponse(
        const QString& task_id,
        int chunk_index,
        QNetworkReply* reply
    ) {
        if (!reply || !m_active_uploads.contains(task_id)) {
            return;
        }

        reply->deleteLater();
        m_active_uploads[task_id].reply = nullptr;

        if (reply->error() != QNetworkReply::NoError) {
            auto json_opt = ParseJsonResponse(reply);
            if (json_opt.has_value() && json_opt->contains("error")) {
                auto err = ErrorAdapter::FromJson(json_opt->value("error").toObject());

                if (err.code == 50008) {
                    ExpireUpload(task_id);
                    return;
                }
                if (err.code == 50004) {
                    err.retryable = false;
                    FailUpload(task_id, err);
                    return;
                }
                if (err.code == 50009) {
                    RetryOrFailUpload(task_id, err, reply);
                    return;
                }
                if (err.code == 10005) {
                    RetryOrFailUpload(task_id, err, reply);
                    return;
                }
                RetryOrFailUpload(task_id, err, reply);
            } else {
                ApiError err = ErrorAdapter::FromNetworkError(reply->error());
                RetryOrFailUpload(task_id, err, reply);
            }
            return;
        }

        // Chunk succeeded — record it
        int row = m_upload_model->FindTask(task_id);
        if (row < 0) {
            return;
        }
        auto task = *m_upload_model->GetTask(row);

        if (!task.uploaded_chunk_indices.contains(chunk_index)) {
            task.uploaded_chunk_indices.append(chunk_index);
        }

        // Calculate progress
        int total = task.total_chunks.value_or(0);
        if (total > 0) {
            double progress =
                static_cast<double>(task.uploaded_chunk_indices.size()) / total;
            emit uploadProgressChanged(task_id, progress);
        }

        m_upload_model->UpdateTask(task_id, task);
        AdvanceToNextChunk(task_id);
    }

    void TransferManager::AdvanceToNextChunk(const QString& task_id) {
        int row = m_upload_model->FindTask(task_id);
        if (row < 0) {
            return;
        }
        auto task = *m_upload_model->GetTask(row);

        if (!task.total_chunks.has_value()) {
            return;
        }

        int total = *task.total_chunks;
        int next_chunk = -1;

        for (int i = 0; i < total; ++i) {
            if (!task.uploaded_chunk_indices.contains(i)) {
                next_chunk = i;
                break;
            }
        }

        if (next_chunk < 0) {
            // All chunks uploaded → Completing
            StartUploadComplete(task_id);
        } else {
            StartChunkUpload(task_id, next_chunk);
        }
    }

    void TransferManager::StartUploadComplete(const QString& task_id) {
        SetUploadState(task_id, UploadState::Completing);

        int row = m_upload_model->FindTask(task_id);
        if (row < 0) {
            return;
        }
        auto task = *m_upload_model->GetTask(row);

        if (!task.upload_id.has_value()) {
            FailUpload(task_id, ApiError{});
            return;
        }

        QJsonObject body;
        body["upload_id"] = *task.upload_id;

        QJsonDocument doc(body);
        auto headers = PrepareOwnerHeaders();
        headers["Content-Type"] = "application/json";

        auto reply = m_network_client->Post(
            QUrl("/api/file/upload/complete"),
            doc.toJson(QJsonDocument::Compact),
            headers
        );

        m_active_uploads[task_id].reply = reply;

        QString tid = task_id;
        connect(reply, &QNetworkReply::finished, this, [this, tid]() {
            HandleCompleteResponse(tid, m_active_uploads.value(tid).reply);
        });
    }

    void TransferManager::HandleCompleteResponse(
        const QString& task_id,
        QNetworkReply* reply
    ) {
        if (!reply || !m_active_uploads.contains(task_id)) {
            return;
        }

        reply->deleteLater();
        m_active_uploads[task_id].reply = nullptr;

        if (reply->error() != QNetworkReply::NoError) {
            auto json_opt = ParseJsonResponse(reply);
            if (json_opt.has_value() && json_opt->contains("error")) {
                auto err = ErrorAdapter::FromJson(json_opt->value("error").toObject());
                if (err.code == 50008) {
                    ExpireUpload(task_id);
                    return;
                }
                if (err.code == 50004) {
                    err.retryable = false;
                    FailUpload(task_id, err);
                    return;
                }
                RetryOrFailUpload(task_id, err, reply);
            } else {
                ApiError err = ErrorAdapter::FromNetworkError(reply->error());
                RetryOrFailUpload(task_id, err, reply);
            }
            return;
        }

        // §6.2: complete success → Completed (reserved → used conversion happens server-side)
        SetUploadState(task_id, UploadState::Completed);
        m_active_uploads.remove(task_id);
        emit uploadProgressChanged(task_id, 1.0);
    }

    void TransferManager::StartUploadCancel(const QString& task_id) {
        int row = m_upload_model->FindTask(task_id);
        if (row < 0) {
            return;
        }
        auto task = *m_upload_model->GetTask(row);

        if (task.status != "cancelling") {
            return;
        }

        if (!task.upload_id.has_value()) {
            SetUploadState(task_id, UploadState::Cancelled);
            m_active_uploads.remove(task_id);

            return;
        }

        auto headers = PrepareOwnerHeaders();
        auto reply = m_network_client->Delete(
            QUrl(QString("/api/file/upload/%1").arg(*task.upload_id)),
            headers
        );

        m_active_uploads[task_id].reply = reply;

        QString tid = task_id;
        connect(reply, &QNetworkReply::finished, this, [this, tid]() {
            HandleCancelResponse(tid, m_active_uploads.value(tid).reply);
        });
    }

    void TransferManager::HandleCancelResponse(
        const QString& task_id,
        QNetworkReply* reply
    ) {
        if (!reply || !m_active_uploads.contains(task_id)) {
            return;
        }

        reply->deleteLater();
        m_active_uploads[task_id].reply = nullptr;

        // §6.5: cancel request itself is retryable for terminal-state tasks
        if (reply->error() != QNetworkReply::NoError) {
            int status =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            // 404 means task already gone — treat as cancelled
            if (status == 404) {
                SetUploadState(task_id, UploadState::Cancelled);
                m_active_uploads.remove(task_id);
    
                return;
            }

            // Retry cancel per §8.2
            auto& active = m_active_uploads[task_id];
            if (active.retry_count < kMaxRetryCount) {
                active.retry_count++;
                QTimer::singleShot(
                    1000 * active.retry_count,
                    this,
                    [this, task_id]() { StartUploadCancel(task_id); }
                );
                return;
            }
        }

        SetUploadState(task_id, UploadState::Cancelled);
        m_active_uploads.remove(task_id);
    }

    void TransferManager::SetUploadState(
        const QString& task_id,
        UploadState state
    ) {
        int row = m_upload_model->FindTask(task_id);
        if (row < 0) {
            return;
        }
        auto task = *m_upload_model->GetTask(row);
        task.status = ToString(state);
        m_upload_model->UpdateTask(task_id, task);
    }

    void TransferManager::FailUpload(
        const QString& task_id,
        const ApiError& error
    ) {
        int row = m_upload_model->FindTask(task_id);
        if (row < 0) {
            return;
        }
        auto task = *m_upload_model->GetTask(row);
        task.status = "failed";
        task.error = error;
        m_upload_model->UpdateTask(task_id, task);
        m_active_uploads.remove(task_id);
        emit taskError(task_id, error.message);
    }

    void TransferManager::ExpireUpload(const QString& task_id) {
        int row = m_upload_model->FindTask(task_id);
        if (row < 0) {
            return;
        }
        auto task = *m_upload_model->GetTask(row);
        task.status = "expired";
        ApiError err;
        err.code = 50008;
        err.family = "file";
        err.category = "UploadSessionExpired";
        err.message = "上传任务已过期或不存在";
        err.retryable = true;
        err.action = "restart_upload";
        task.error = err;
        task.upload_id = std::nullopt;
        m_upload_model->UpdateTask(task_id, task);
        m_active_uploads.remove(task_id);
        emit taskError(task_id, err.message);
    }

    void TransferManager::RetryOrFailUpload(
        const QString& task_id,
        const ApiError& error,
        QNetworkReply* reply
    ) {
        if (!m_active_uploads.contains(task_id)) {
            FailUpload(task_id, error);
            return;
        }

        auto& active = m_active_uploads[task_id];
        const auto status_code = reply
            ? reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
            : 0;
        const bool rate_limited = status_code == 429 || error.code == 10005;

        int delay_ms = 0;
        if (rate_limited && reply) {
            const auto retry_after = reply->rawHeader("Retry-After").trimmed();
            bool ok = false;
            int seconds = retry_after.toInt(&ok);
            if (ok && seconds > 0) {
                delay_ms = seconds * 1000;
            } else {
                const auto reset_header = reply->rawHeader("X-RateLimit-Reset").trimmed();
                const qint64 reset_time = reset_header.toLongLong(&ok);
                if (ok && reset_time > 0) {
                    const qint64 now = QDateTime::currentSecsSinceEpoch();
                    delay_ms = static_cast<int>(qMax<qint64>(1, reset_time - now) * 1000);
                }
            }
        }

        const bool can_retry = (error.retryable || rate_limited) &&
                               (rate_limited || active.retry_count < kMaxRetryCount);
        if (!can_retry) {
            FailUpload(task_id, error);
            return;
        }

        if (!rate_limited) {
            active.retry_count++;
        }
        if (delay_ms <= 0) {
            delay_ms = 1000 * (1 << qMin(active.retry_count, kMaxRetryCount - 1));
        }

        int row = m_upload_model->FindTask(task_id);
        if (row >= 0) {
            auto task = *m_upload_model->GetTask(row);
            task.status = "retrying";
            task.error = error;
            m_upload_model->UpdateTask(task_id, task);
        }

        QTimer::singleShot(delay_ms, this, [this, task_id]() {
            int r = m_upload_model->FindTask(task_id);
            if (r < 0) {
                return;
            }
            auto t = *m_upload_model->GetTask(r);

            if (t.status != "retrying") {
                return;
            }

            if (!t.upload_id.has_value()) {
                StartUploadInit(task_id);
                return;
            }
            if (t.total_chunks.has_value() &&
                t.uploaded_chunk_indices.size() >= *t.total_chunks) {
                StartUploadComplete(task_id);
                return;
            }
            AdvanceToNextChunk(task_id);
        });
    }

    // ── Download Internal ──

    auto TransferManager::CreateDownloadTask(
        quint64 file_id,
        const QString& target_path,
        const QString& auth_domain,
        const QString& filename,
        quint64 file_size,
        const QString& share_id
    ) -> QString {
        DownloadTask task;
        task.task_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        task.auth_domain = auth_domain;
        task.share_id = share_id.isEmpty() ? std::nullopt : std::optional<QString>(share_id);
        task.file_id = file_id;
        task.filename = filename.isEmpty() ? QString("file_%1").arg(file_id) : filename;
        task.file_size = file_size;
        task.target_path = target_path;
        task.status = "queued";
        task.transfer_mode = "full";
        task.supports_range = false;

        m_download_model->AddTask(task);
        return task.task_id;
    }

    void TransferManager::FetchDownloadMetadata(const QString& task_id) {
        int row = m_download_model->FindTask(task_id);
        if (row < 0) {
            return;
        }
        auto task = *m_download_model->GetTask(row);

        auto headers = PrepareOwnerHeaders();
        auto reply = m_network_client->Get(
            QUrl(QString("/api/file/download/%1/info").arg(task.file_id)),
            headers
        );

        ActiveDownload active;
        active.reply = reply;
        m_active_downloads[task_id] = active;

        QString tid = task_id;
        connect(reply, &QNetworkReply::finished, this, [this, tid]() {
            HandleMetadataResponse(tid, m_active_downloads.value(tid).reply);
        });
    }

    void TransferManager::HandleMetadataResponse(
        const QString& task_id,
        QNetworkReply* reply
    ) {
        if (!reply || !m_active_downloads.contains(task_id)) {
            return;
        }

        reply->deleteLater();
        m_active_downloads[task_id].reply = nullptr;

        if (reply->error() != QNetworkReply::NoError) {
            auto json_opt = ParseJsonResponse(reply);
            if (json_opt.has_value() && json_opt->contains("error")) {
                auto err = ErrorAdapter::FromJson(json_opt->value("error").toObject());
                RetryOrFailDownload(task_id, err);
            } else {
                ApiError err = ErrorAdapter::FromNetworkError(reply->error());
                RetryOrFailDownload(task_id, err);
            }
            return;
        }

        auto json_opt = ParseJsonResponse(reply);
        if (!json_opt.has_value()) {
            ApiError err;
            err.code = 0;
            err.family = "general";
            err.category = "ParseError";
            err.message = "无效的元数据响应";
            err.retryable = true;
            err.action = "retry";
            RetryOrFailDownload(task_id, err);
            return;
        }

        auto data = json_opt->value("data").toObject();
        int row = m_download_model->FindTask(task_id);
        if (row < 0) {
            return;
        }
        auto task = *m_download_model->GetTask(row);

        task.filename = data.value("filename").toString(task.filename);
        task.file_size =
            static_cast<quint64>(data.value("file_size").toDouble(task.file_size));
        task.file_hash = data.value("file_hash").toString();
        task.mime_type = data.value("mime_type").toString();
        task.supports_range = data.value("supports_range").toBool(false);

        // §7.3: Check for partial file to determine full vs range
        QFileInfo partial_info(task.target_path);
        if (partial_info.exists() && partial_info.size() > 0 &&
            task.supports_range) {
            task.transfer_mode = "range";
            task.range_start = static_cast<quint64>(partial_info.size());
            task.received_bytes = task.range_start.value_or(0);
        } else {
            task.transfer_mode = "full";
            if (partial_info.exists()) {
                QFile::remove(task.target_path);
            }
        }

        m_download_model->UpdateTask(task_id, task);
        SetDownloadState(task_id, DownloadState::Ready);
        StartDownloadTransfer(task_id);
    }

    void TransferManager::StartDownloadTransfer(const QString& task_id) {
        int row = m_download_model->FindTask(task_id);
        if (row < 0) {
            return;
        }
        auto task = *m_download_model->GetTask(row);

        // Open file for writing
        auto* file = new QFile(task.target_path);
        QIODevice::OpenMode open_mode = QIODevice::WriteOnly;
        if (task.transfer_mode == "range") {
            open_mode = QIODevice::WriteOnly | QIODevice::Append;
        }

        if (!file->open(open_mode)) {
            delete file;
            ApiError err;
            err.code = 0;
            err.family = "general";
            err.category = "LocalIOError";
            err.message = "无法打开文件进行写入";
            err.retryable = false;
            err.action = "fix_request";
            FailDownload(task_id, err);
            return;
        }

        // Build request
        auto headers = task.auth_domain == "owner" ? PrepareOwnerHeaders() : PrepareVisitorHeaders();

        QUrl url;
        if (task.auth_domain == "owner") {
            url = QUrl(QString("/api/file/download/%1").arg(task.file_id));
        } else {
            url = QUrl(QString("/api/share/download/%1/%2")
                           .arg(task.share_id.value_or(""), QString::number(task.file_id)));
        }

        // §7.3: Add Range header for resume — must be in headers map
        // (NetworkClient::Get builds its own QNetworkRequest from these headers)
        if (task.transfer_mode == "range" && task.range_start.has_value()) {
            headers["Range"] = QString("bytes=%1-")
                                   .arg(static_cast<qint64>(*task.range_start));
            SetDownloadState(task_id, DownloadState::TransferringRange);
        } else {
            SetDownloadState(task_id, DownloadState::TransferringFull);
        }

        auto reply = m_network_client->Get(url, headers);

        m_active_downloads[task_id].reply = reply;
        m_active_downloads[task_id].file = file;
        m_active_downloads[task_id].bytes_received_at_start = task.received_bytes;
        m_active_downloads[task_id].download_start_time =
            QDateTime::currentSecsSinceEpoch();

        QString tid = task_id;
        connect(reply, &QNetworkReply::readyRead, this, [this, tid]() {
            HandleDownloadData(tid);
        });
        connect(reply, &QNetworkReply::finished, this, [this, tid]() {
            HandleDownloadFinished(tid);
        });
    }

    void TransferManager::HandleDownloadData(const QString& task_id) {
        if (!m_active_downloads.contains(task_id)) {
            return;
        }

        auto& active = m_active_downloads[task_id];
        if (!active.reply || !active.file) {
            return;
        }

        qint64 bytes = active.reply->bytesAvailable();
        if (bytes <= 0) {
            return;
        }

        QByteArray data = active.reply->read(bytes);
        active.file->write(data);

        int row = m_download_model->FindTask(task_id);
        if (row < 0) {
            return;
        }
        auto task = *m_download_model->GetTask(row);
        task.received_bytes = active.bytes_received_at_start +
                              static_cast<quint64>(active.file->pos());
        m_download_model->UpdateTask(task_id, task);

        if (task.file_size > 0) {
            double progress =
                static_cast<double>(task.received_bytes) / task.file_size;
            emit downloadProgressChanged(task_id, progress);
        }
    }

    void TransferManager::HandleDownloadFinished(const QString& task_id) {
        if (!m_active_downloads.contains(task_id)) {
            return;
        }

        auto& active = m_active_downloads[task_id];
        auto* reply = active.reply;
        auto* file = active.file;

        active.reply = nullptr;
        active.file = nullptr;

        if (reply) {
            reply->deleteLater();
        }

        if (file) {
            file->close();
            delete file;
        }

        if (!reply) {
            return;
        }

        // §7.4: Handle 416 Range Not Satisfiable
        int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 416) {
            int row = m_download_model->FindTask(task_id);
            if (row >= 0) {
                auto task = *m_download_model->GetTask(row);
                QFile::remove(task.target_path);
                task.transfer_mode = "full";
                task.range_start = std::nullopt;
                task.received_bytes = 0;
                m_download_model->UpdateTask(task_id, task);
            }
            SetDownloadState(task_id, DownloadState::FetchingMetadata);
            FetchDownloadMetadata(task_id);
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            auto json_opt = ParseJsonResponse(reply);
            if (json_opt.has_value() && json_opt->contains("error")) {
                auto err = ErrorAdapter::FromJson(json_opt->value("error").toObject());
                RetryOrFailDownload(task_id, err);
            } else {
                ApiError err = ErrorAdapter::FromNetworkError(reply->error());
                RetryOrFailDownload(task_id, err);
            }
            return;
        }

        // Download complete
        int row = m_download_model->FindTask(task_id);
        if (row >= 0) {
            auto task = *m_download_model->GetTask(row);
            task.received_bytes = task.file_size;
            m_download_model->UpdateTask(task_id, task);
        }

        SetDownloadState(task_id, DownloadState::Completed);
        m_active_downloads.remove(task_id);
        emit downloadProgressChanged(task_id, 1.0);
    }

    void TransferManager::SetDownloadState(
        const QString& task_id,
        DownloadState state
    ) {
        int row = m_download_model->FindTask(task_id);
        if (row < 0) {
            return;
        }
        auto task = *m_download_model->GetTask(row);
        task.status = ToString(state);
        m_download_model->UpdateTask(task_id, task);
    }

    void TransferManager::FailDownload(
        const QString& task_id,
        const ApiError& error
    ) {
        int row = m_download_model->FindTask(task_id);
        if (row < 0) {
            return;
        }
        auto task = *m_download_model->GetTask(row);
        task.status = "failed";
        task.error = error;
        m_download_model->UpdateTask(task_id, task);

        if (m_active_downloads.contains(task_id)) {
            auto& active = m_active_downloads[task_id];
            if (active.file) {
                active.file->close();
                delete active.file;
                active.file = nullptr;
            }
        }
        m_active_downloads.remove(task_id);
        emit taskError(task_id, error.message);
    }

    void TransferManager::RetryOrFailDownload(
        const QString& task_id,
        const ApiError& error
    ) {
        // Close file on retry
        if (m_active_downloads.contains(task_id)) {
            auto& active = m_active_downloads[task_id];
            if (active.file) {
                active.file->close();
                delete active.file;
                active.file = nullptr;
            }
            if (active.reply) {
                active.reply->deleteLater();
                active.reply = nullptr;
            }
        }

        if (!m_active_downloads.contains(task_id)) {
            FailDownload(task_id, error);
            return;
        }

        auto& active = m_active_downloads[task_id];
        if (active.retry_count < kMaxRetryCount && error.retryable) {
            active.retry_count++;
            int delay_ms = 1000 * (1 << (active.retry_count - 1));

            SetDownloadState(task_id, DownloadState::RetryWaiting);

            QTimer::singleShot(delay_ms, this, [this, task_id]() {
                if (!m_active_downloads.contains(task_id)) {
                    return;
                }

                int row = m_download_model->FindTask(task_id);
                if (row < 0) {
                    return;
                }
                auto task = *m_download_model->GetTask(row);

                // §7.3: resume path — re-fetch metadata to recalculate offset
                SetDownloadState(task_id, DownloadState::FetchingMetadata);
                if (task.auth_domain == "owner") {
                    FetchDownloadMetadata(task_id);
                } else {
                    PreparePartialFileForRange(task_id);
                    SetDownloadState(task_id, DownloadState::Ready);
                    StartDownloadTransfer(task_id);
                }
            });
        } else {
            FailDownload(task_id, error);
        }
    }

    void TransferManager::PreparePartialFileForRange(const QString& task_id) {
        int row = m_download_model->FindTask(task_id);
        if (row < 0) {
            return;
        }
        auto task = *m_download_model->GetTask(row);

        QFileInfo info(task.target_path);
        if (info.exists() && info.size() > 0 && task.supports_range) {
            task.transfer_mode = "range";
            task.range_start = static_cast<quint64>(info.size());
            task.received_bytes = task.range_start.value_or(0);
        } else {
            task.transfer_mode = "full";
            if (info.exists()) {
                QFile::remove(task.target_path);
            }
            task.range_start = std::nullopt;
            task.received_bytes = 0;
        }
        m_download_model->UpdateTask(task_id, task);
    }

    // ── Utility ──

    auto TransferManager::ComputeFileMd5(const QString& file_path) -> QString {
        QFile file(file_path);
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }

        QCryptographicHash hash(QCryptographicHash::Md5);
        constexpr qint64 buffer_size{ 8192 };

        while (!file.atEnd()) {
            QByteArray data = file.read(buffer_size);
            if (data.isEmpty()) {
                break;
            }
            hash.addData(data);
        }

        return QString(hash.result().toHex());
    }

    void TransferManager::AbortActiveUpload(const QString& task_id) {
        if (!m_active_uploads.contains(task_id)) {
            return;
        }
        auto& active = m_active_uploads[task_id];
        if (active.reply) {
            active.reply->abort();
            active.reply->deleteLater();
            active.reply = nullptr;
        }
    }

    void TransferManager::ClearHashWatcher(const QString& task_id) {
        auto* watcher = m_hash_watchers.take(task_id);
        if (!watcher) {
            return;
        }
        watcher->cancel();
        watcher->deleteLater();
    }

    void TransferManager::AbortActiveDownload(const QString& task_id) {
        if (!m_active_downloads.contains(task_id)) {
            return;
        }
        auto& active = m_active_downloads[task_id];
        if (active.reply) {
            active.reply->abort();
            active.reply->deleteLater();
            active.reply = nullptr;
        }
        if (active.file) {
            active.file->flush();
            active.file->close();
            delete active.file;
            active.file = nullptr;
        }
    }

    auto TransferManager::ParseJsonResponse(QNetworkReply* reply)
        -> std::optional<QJsonObject> {
        if (!reply) {
            return std::nullopt;
        }
        QByteArray data = reply->readAll();
        QJsonParseError error;
        auto doc = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError) {
            return std::nullopt;
        }
        if (!doc.isObject()) {
            return std::nullopt;
        }
        return doc.object();
    }

    auto TransferManager::IsRetryableError(QNetworkReply::NetworkError error) -> bool {
        switch (error) {
            case QNetworkReply::ConnectionRefusedError:
            case QNetworkReply::RemoteHostClosedError:
            case QNetworkReply::TimeoutError:
            case QNetworkReply::TemporaryNetworkFailureError:
            case QNetworkReply::NetworkSessionFailedError:
            case QNetworkReply::BackgroundRequestNotAllowedError:
            case QNetworkReply::TooManyRedirectsError:
            case QNetworkReply::InsecureRedirectError:
            case QNetworkReply::UnknownNetworkError:
                return true;
            default:
                return false;
        }
    }

    auto TransferManager::IsServerError(int status_code) -> bool {
        return status_code >= 500 && status_code < 600;
    }

    auto TransferManager::PrepareOwnerHeaders() -> QMap<QString, QString> {
        return m_request_factory->PrepareHeaders(AuthDomain::Owner);
    }

    auto TransferManager::PrepareVisitorHeaders() -> QMap<QString, QString> {
        return m_request_factory->PrepareHeaders(AuthDomain::Visitor);
    }

} // namespace disk::desktop::managers
