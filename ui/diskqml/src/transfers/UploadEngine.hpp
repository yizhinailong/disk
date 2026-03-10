/**
 * @file UploadEngine.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Chunked upload engine: Init → Chunk → Complete with Pause/Cancel
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Drives a single file upload through the backend's chunked upload flow:
 *   1. Init:     POST /api/file/upload/init      (JSON)
 *   2. Chunk:    POST /api/file/upload/chunk      (query params + raw body)
 *   3. Complete: POST /api/file/upload/complete   (JSON)
 *   4. Cancel:   DELETE /api/file/upload/{id}
 *
 * Reads the file per-chunk (does NOT load entire file into memory).
 * Uses QCryptographicHash::Md5 for file_hash and chunk_hash.
 * Reports progress to TransferQueueModel via UpdateProgress/UpdateStatus.
 */

#pragma once

#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QString>

#include <transfers/TransferItem.hpp>

namespace disk::qml::api {
    class ApiClient;
} // namespace disk::qml::api

namespace disk::qml::transfers {

    class TransferQueueModel;

    /**
     * @brief Drives a single file upload through the backend's chunked upload flow.
     *
     * @details
     * Lifecycle:
     *   - Construct with file path, parent folder ID, and pointers to ApiClient/TransferQueueModel.
     *   - Call Start() to begin the upload.
     *   - Call Pause() to pause after the current chunk completes.
     *   - Call Cancel() to abort and notify the backend.
     *   - finished() signal is emitted when upload completes, fails, or is cancelled.
     *
     * One UploadEngine per file. A TransferManager would create and manage these.
     */
    class UploadEngine : public QObject {
        Q_OBJECT

    public:
        /**
         * @brief Construct an upload engine.
         *
         * @param filePath     Local file path to upload.
         * @param parentId     Backend parent folder ID (0 = root).
         * @param apiClient    Shared ApiClient (not owned).
         * @param queueModel   TransferQueueModel to report progress (not owned).
         * @param parent       QObject parent.
         */
        explicit UploadEngine(
            const QString& filePath,
            quint64 parentId,
            api::ApiClient* apiClient,
            TransferQueueModel* queueModel,
            QObject* parent = nullptr
        );

        ~UploadEngine() override;

        /// @brief Start (or resume) the upload.
        void Start();

        /// @brief Pause after the current chunk finishes.
        void Pause();

        /// @brief Cancel the upload and notify the backend.
        void Cancel();

        /// @brief Transfer item ID (UUID) for this upload.
        [[nodiscard]] auto TransferId() const -> const QString&;

        /// @brief Get the transfer item data.
        [[nodiscard]] auto Item() const -> const TransferItem& { return m_transfer_item; }

        /// @brief Get the original file path being uploaded.
        [[nodiscard]] auto FilePath() const -> const QString& { return m_file_path; }

        /// @brief Get the parent folder ID for this upload.
        [[nodiscard]] auto ParentId() const -> quint64 { return m_parent_id; }

    signals:
        /// @brief Emitted when the upload finishes (success, failure, or cancel).
        void finished(const QString& transferId, bool success, const QString& error);

    private:
        // ----- Upload flow steps -----

        /// Compute MD5 hash of the entire file (async-friendly: called from Start).
        void ComputeFileHash();

        /// POST /api/file/upload/init
        void SendInit();

        /// Process the init response (may be instant upload or normal).
        void HandleInitResponse(bool netErr, const QString& errStr, int status, const QByteArray& body);

        /// Send the next queued chunk.
        void SendNextChunk();

        /// Read a chunk from the file and compute its MD5 hash.
        auto ReadChunk(quint32 chunkIndex) -> QPair<QByteArray, QString>;

        /// Process a chunk upload response.
        void HandleChunkResponse(quint32 chunkIndex, bool netErr, const QString& errStr, int status, const QByteArray& body);

        /// POST /api/file/upload/complete
        void SendComplete();

        /// Process the complete response.
        void HandleCompleteResponse(bool netErr, const QString& errStr, int status, const QByteArray& body);

        /// DELETE /api/file/upload/{upload_id}
        void SendCancel();

        // ----- Progress / state helpers -----

        void UpdateProgress(qint64 doneBytes);
        void SetFailed(const QString& error);
        void SetCompleted();
        void SetPaused();

        // ----- Data -----

        QString m_file_path;
        quint64 m_parent_id{ 0 };
        api::ApiClient* m_api_client{ nullptr };
        TransferQueueModel* m_queue_model{ nullptr };

        // Transfer state
        TransferItem m_transfer_item;
        QFile m_file;
        QString m_file_hash; ///< MD5 hex digest of entire file
        QString m_upload_id; ///< Backend-assigned upload ID
        quint32 m_chunk_size{ 0 };
        quint32 m_total_chunks{ 0 };
        quint32 m_next_chunk{ 0 };
        QVector<quint32> m_uploaded_chunks; ///< Already-uploaded indices (for resume)

        // Control flags
        bool m_paused{ false };
        bool m_cancelled{ false };
        bool m_in_flight{ false }; ///< True when a network request is pending

        // Speed/ETA tracking
        QElapsedTimer m_speed_timer;
        qint64 m_bytes_at_speed_start{ 0 };

        static constexpr quint32 kDefaultChunkSize = 5 * 1024 * 1024; // 5 MB
    };

} // namespace disk::qml::transfers
