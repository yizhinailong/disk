/**
 * @file TransferManager.hpp
 * @brief Upload/download task coordination per doc 03 §6-7
 *
 * Coordinates all upload and download tasks following the state machines
 * defined in docs/desktop/03-auth-network-and-transfers.md §6-7.
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <QCryptographicHash>
#include <QFile>
#include <QMap>
#include <QNetworkReply>
#include <QObject>
#include <QTimer>
#include <QUuid>
#include <functional>

#include "models/DownloadTaskModel.hpp"
#include "models/UploadTaskModel.hpp"
#include "network/ErrorAdapter.hpp"
#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

namespace disk::desktop::managers {

    // ── Upload state machine per doc 03 §6.1 ──

    enum class UploadState {
        Queued,
        Hashing,
        Initializing,
        InstantUploaded,
        Resuming,
        Uploading,
        Completing,
        CancelPending,
        Completed,
        Cancelled,
        Expired,
        Failed,
    };

    auto ToString(UploadState state) -> QString;

    // ── Download state machine per doc 03 §7.1 ──

    enum class DownloadState {
        Idle,
        FetchingMetadata,
        Ready,
        TransferringFull,
        TransferringRange,
        Paused,
        RetryWaiting,
        Completed,
        Cancelled,
        Failed,
    };

    auto ToString(DownloadState state) -> QString;

    // ── Retry constants ──

    static constexpr int kMaxRetryCount{ 3 };
    static constexpr int kDefaultChunkSize{ 5 * 1024 * 1024 }; // 5 MB

    /**
     * @brief TransferManager coordinates all upload and download tasks
     *
     * Provides QML-invokable methods for starting, pausing, resuming,
     * cancelling, and retrying transfers. Maintains UploadTaskModel and
     * DownloadTaskModel as the canonical data source for QML views.
     *
     * State transitions follow doc 03 §6.2 (upload) and §7.3 (download).
     */
    class TransferManager : public QObject {
        Q_OBJECT

        Q_PROPERTY(UploadTaskModel* uploadModel READ GetUploadModel CONSTANT)
        Q_PROPERTY(DownloadTaskModel* downloadModel READ GetDownloadModel CONSTANT)
        Q_PROPERTY(quint64 localReservedBytes READ GetLocalReservedBytes NOTIFY localReservedChanged)

    public:
        explicit TransferManager(
            NetworkClient* network_client,
            RequestFactory* request_factory,
            QObject* parent = nullptr
        );
        ~TransferManager() override;

        auto GetUploadModel() -> UploadTaskModel*;
        auto GetDownloadModel() -> DownloadTaskModel*;
        auto GetLocalReservedBytes() const -> quint64;

        // ── Upload operations ──

        /**
         * @brief Start a new upload task
         * @param local_path Local file absolute path
         * @param parent_id Target directory ID
         * Creates UploadTask in Queued state, then transitions to Hashing.
         */
        Q_INVOKABLE void StartUpload(const QString& local_path, quint64 parent_id);

        /**
         * @brief Retry a failed upload
         * @param task_id Task to retry
         * Re-enters Hashing state from Failed/Expired.
         */
        Q_INVOKABLE void RetryUpload(const QString& task_id);

        /**
         * @brief Cancel an active upload
         * @param task_id Task to cancel
         * Transitions to CancelPending, sends DELETE to backend.
         */
        Q_INVOKABLE void CancelUpload(const QString& task_id);

        // ── Download operations ──

        /**
         * @brief Start a new download task
         * @param file_id Remote file ID
         * @param target_path Local save path
         * @param auth_domain "owner" or "visitor"
         * Creates DownloadTask, fetches metadata, then starts transfer.
         */
        Q_INVOKABLE void StartDownload(
            quint64 file_id,
            const QString& target_path,
            const QString& auth_domain = "owner"
        );

        /**
         * @brief Start a visitor download
         * @param file_id Remote file ID
         * @param target_path Local save path
         * @param share_id Share ID for visitor context
         * @param filename Display filename
         * @param file_size Expected file size
         */
        Q_INVOKABLE void StartVisitorDownload(
            quint64 file_id,
            const QString& target_path,
            const QString& share_id,
            const QString& filename,
            quint64 file_size
        );

        /**
         * @brief Pause an active download
         * @param task_id Task to pause
         * Aborts reply, keeps partial file.
         */
        Q_INVOKABLE void PauseDownload(const QString& task_id);

        /**
         * @brief Resume a paused download
         * @param task_id Task to resume
         * Re-enters FetchingMetadata to recalculate offset.
         */
        Q_INVOKABLE void ResumeDownload(const QString& task_id);

        /**
         * @brief Cancel a download
         * @param task_id Task to cancel
         * Aborts reply, deletes partial file.
         */
        Q_INVOKABLE void CancelDownload(const QString& task_id);

        /**
         * @brief Retry a failed download
         * @param task_id Task to retry
         * Re-enters FetchingMetadata from Failed.
         */
        Q_INVOKABLE void RetryDownload(const QString& task_id);

        /**
         * @brief Clear completed upload tasks
         */
        Q_INVOKABLE void ClearCompletedUploads();

        /**
         * @brief Clear completed download tasks
         */
        Q_INVOKABLE void ClearCompletedDownloads();

        /**
         * @brief Cancel all non-terminal owner transfer work locally
         *
         * Used during logout or forced reauthentication so active owner
         * uploads/downloads do not continue after session teardown.
         */
        void ShutdownOwnerTransfers();

    signals:
        void localReservedChanged();
        void uploadProgressChanged(const QString& task_id, double progress);
        void downloadProgressChanged(const QString& task_id, double progress);
        void uploadSpeedChanged(const QString& task_id, qint64 bytes_per_sec);
        void downloadSpeedChanged(const QString& task_id, qint64 bytes_per_sec);
        void taskError(const QString& task_id, const QString& message);

    private:
        // ── Upload internal methods ──

        auto CreateUploadTask(const QString& local_path, quint64 parent_id) -> QString;
        void StartHashing(const QString& task_id);
        void StartUploadInit(const QString& task_id);
        void HandleInitResponse(const QString& task_id, QNetworkReply* reply);
        void StartChunkUpload(const QString& task_id, int chunk_index);
        void HandleChunkResponse(
            const QString& task_id,
            int chunk_index,
            QNetworkReply* reply
        );
        void StartUploadComplete(const QString& task_id);
        void HandleCompleteResponse(const QString& task_id, QNetworkReply* reply);
        void StartUploadCancel(const QString& task_id);
        void HandleCancelResponse(const QString& task_id, QNetworkReply* reply);

        void SetUploadState(const QString& task_id, UploadState state);
        void FailUpload(const QString& task_id, const ApiError& error);
        void ExpireUpload(const QString& task_id);
        void RetryOrFailUpload(const QString& task_id, const ApiError& error);
        void AdvanceToNextChunk(const QString& task_id);

        // ── Download internal methods ──

        auto CreateDownloadTask(
            quint64 file_id,
            const QString& target_path,
            const QString& auth_domain,
            const QString& filename = {},
            quint64 file_size = 0
        ) -> QString;
        void FetchDownloadMetadata(const QString& task_id);
        void HandleMetadataResponse(const QString& task_id, QNetworkReply* reply);
        void StartDownloadTransfer(const QString& task_id);
        void HandleDownloadData(const QString& task_id);
        void HandleDownloadFinished(const QString& task_id);

        void SetDownloadState(const QString& task_id, DownloadState state);
        void FailDownload(const QString& task_id, const ApiError& error);
        void RetryOrFailDownload(const QString& task_id, const ApiError& error);
        void PreparePartialFileForRange(const QString& task_id);

        // ── Utility ──

        auto ComputeFileMd5(const QString& file_path) -> QString;
        void AbortActiveUpload(const QString& task_id);
        void AbortActiveDownload(const QString& task_id);
        auto ParseJsonResponse(QNetworkReply* reply) -> std::optional<QJsonObject>;
        auto IsRetryableError(QNetworkReply::NetworkError error) -> bool;
        auto IsServerError(int status_code) -> bool;
        auto PrepareOwnerHeaders() -> QMap<QString, QString>;
        auto PrepareVisitorHeaders() -> QMap<QString, QString>;

        // ── Active reply tracking ──

        struct ActiveUpload {
            QNetworkReply* reply{ nullptr };
            int retry_count{ 0 };
            int current_chunk{ 0 };
            qint64 bytes_sent{ 0 };
            qint64 chunk_start_time{ 0 };
        };

        struct ActiveDownload {
            QNetworkReply* reply{ nullptr };
            QFile* file{ nullptr };
            int retry_count{ 0 };
            qint64 bytes_received_at_start{ 0 };
            qint64 download_start_time{ 0 };
        };

        NetworkClient* m_network_client;
        RequestFactory* m_request_factory;

        UploadTaskModel* m_upload_model;
        DownloadTaskModel* m_download_model;

        QMap<QString, ActiveUpload> m_active_uploads;
        QMap<QString, ActiveDownload> m_active_downloads;
    };

} // namespace disk::desktop::managers
