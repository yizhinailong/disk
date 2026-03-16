/**
 * @file UploadEngine.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Chunked upload engine: Init → Chunk → Complete with Pause/Cancel
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
        // ----- 上传流程步骤 -----

        /// 计算整个文件的 MD5 哈希（异步友好：从 Start 调用）。
        void ComputeFileHash();

        /// POST /api/file/upload/init
        void SendInit();

        /// 处理初始化响应（可能是秒传或正常上传）。
        void HandleInitResponse(bool netErr, const QString& errStr, int status, const QByteArray& body);

        /// 发送下一个排队的分片。
        void SendNextChunk();

        /// 从文件读取分片并计算其 MD5 哈希。
        auto ReadChunk(quint32 chunkIndex) -> QPair<QByteArray, QString>;

        /// 处理分片上传响应。
        void HandleChunkResponse(quint32 chunkIndex, bool netErr, const QString& errStr, int status, const QByteArray& body);

        /// POST /api/file/upload/complete
        void SendComplete();

        /// 处理完成响应。
        void HandleCompleteResponse(bool netErr, const QString& errStr, int status, const QByteArray& body);

        /// DELETE /api/file/upload/{upload_id}
        void SendCancel();

        // ----- 进度/状态辅助函数 -----

        void UpdateProgress(qint64 doneBytes);
        void SetFailed(const QString& error);
        void SetCompleted();
        void SetPaused();

        // ----- 数据 -----

        QString m_file_path;
        quint64 m_parent_id{ 0 };
        api::ApiClient* m_api_client{ nullptr };
        TransferQueueModel* m_queue_model{ nullptr };

        // 传输状态
        TransferItem m_transfer_item;
        QFile m_file;
        QString m_file_hash; ///< 整个文件的 MD5 十六进制摘要
        QString m_upload_id; ///< 后端分配的上传 ID
        quint32 m_chunk_size{ 0 };
        quint32 m_total_chunks{ 0 };
        quint32 m_next_chunk{ 0 };
        QVector<quint32> m_uploaded_chunks; ///< 已上传的分片索引（用于断点续传）

        // 控制标志
        bool m_paused{ false };
        bool m_cancelled{ false };
        bool m_in_flight{ false }; ///< 当网络请求待处理时为 true

        // 速度/ETA 追踪
        QElapsedTimer m_speed_timer;
        qint64 m_bytes_at_speed_start{ 0 };

        static constexpr quint32 kDefaultChunkSize = 5 * 1024 * 1024; // 5 MB
    };

} // namespace disk::qml::transfers
