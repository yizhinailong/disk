/**
 * @file DownloadEngine.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Download engine: stream-to-disk via Range requests with pause/cancel/resume
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Drives a single file download:
 *  1. GET /api/file/download/{file_id}/info  → filename, size, hash, mime_type, supports_range
 *  2. GET /api/file/download/{file_id}       → stream QNetworkReply to a .part file
 *
 * Supports:
 *  - Resume via Range: bytes=<offset>- header
 *  - Progress / speed / ETA reporting (via TransferItem fields)
 *  - Pause / Cancel operations
 *  - Finalise by renaming .part → final file on success
 *
 * Does NOT load the entire file into memory — data is streamed to disk as it arrives.
 */

#pragma once

#include <QElapsedTimer>
#include <QFile>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QTimer>

#include <transfers/TransferItem.hpp>

namespace disk::qml::api {
    class ApiClient;
}

namespace disk::qml::transfers {

    /**
     * @brief Info returned by the download-info endpoint.
     */
    struct DownloadInfo {
        qint64 fileId{};
        QString filename;
        qint64 fileSize{};
        QString fileHash;
        QString mimeType;
        bool supportsRange{ true };
    };

    /**
     * @brief Callback for DownloadInfo fetch.
     * @param info  Present on success; std::nullopt on failure.
     * @param error Human-readable error message (empty on success).
     */
    using DownloadInfoCallback = std::function<void(std::optional<DownloadInfo> info, QString error)>;

    /**
     * @brief Callback for progress updates during download.
     * @param item  Snapshot of the current TransferItem state (doneBytes, speed, eta, status).
     */
    using DownloadProgressCallback = std::function<void(const TransferItem& item)>;

    /**
     * @brief Callback invoked when a download finishes (success, failure, or cancel).
     * @param item  Final TransferItem state.
     */
    using DownloadFinishedCallback = std::function<void(const TransferItem& item)>;

    /**
     * @brief Engine for downloading a single file to disk with streaming, Range support, and pause/cancel.
     *
     * @details
     * Workflow:
     *  1. Call FetchInfo() to query download metadata.
     *  2. Call Start() to begin streaming.
     *  3. Optionally call Pause() / Resume() / Cancel() while running.
     *  4. On success the .part file is renamed to the final name.
     *
     * The engine writes to `<destDir>/<filename>.part` during transfer.
     * On successful completion, it renames to `<destDir>/<filename>`.
     *
     * One DownloadEngine instance handles exactly one download. Create a new instance per file.
     */
    class DownloadEngine : public QObject {
        Q_OBJECT

    public:
        /**
         * @brief Construct a DownloadEngine.
         *
         * @param client   Shared ApiClient (must outlive this engine). Used for auth headers and NAM.
         * @param parent   QObject parent.
         */
        explicit DownloadEngine(api::ApiClient* client, QObject* parent = nullptr);
        ~DownloadEngine() override;

        /**
         * @brief Fetch download metadata from GET /api/file/download/{file_id}/info.
         *
         * @param fileId  Backend file ID.
         * @param cb      Invoked once with the parsed info or an error.
         */
        auto FetchInfo(qint64 fileId, DownloadInfoCallback cb) -> void;

        /**
         * @brief Start (or resume) downloading the file.
         *
         * @param info     Download metadata previously obtained via FetchInfo().
         * @param destDir  Local directory where the file will be saved.
         * @param progressCb  Called periodically with updated TransferItem state.
         * @param finishedCb  Called once when the download completes, fails, or is cancelled.
         */
        auto Start(
            const DownloadInfo& info,
            const QString& destDir,
            DownloadProgressCallback progressCb,
            DownloadFinishedCallback finishedCb
        ) -> void;

        /**
         * @brief Pause the current download. Can be resumed later via Resume().
         */
        auto Pause() -> void;

        /**
         * @brief Resume a paused download.
         */
        auto Resume() -> void;

        /**
         * @brief Cancel the current download. Deletes the .part file.
         */
        auto Cancel() -> void;

        /**
         * @brief Get a read-only snapshot of the current transfer item state.
         */
        [[nodiscard]] auto Item() const -> const TransferItem&;

    signals:
        /**
         * @brief Emitted when the TransferItem state changes (progress, status, speed, eta).
         */
        void itemChanged(const TransferItem& item);

    private:
        /// Start the network request (with optional Range header for resume)
        auto StartRequest() -> void;

        /// Finalize: rename .part → final, set status=Completed
        auto Finalize() -> void;

        /// Mark failure with error message
        auto Fail(const QString& error) -> void;

        /// Recalculate speed/eta from elapsed timer
        auto UpdateSpeedAndEta() -> void;

        /// Emit progress update
        auto EmitProgress() -> void;

        // ----- Members -----

        api::ApiClient* m_client;

        TransferItem m_item;
        DownloadInfo m_info;
        QString m_dest_dir;
        QString m_part_path;  ///< Full path to the .part file
        QString m_final_path; ///< Full path to the final file

        QFile m_file;
        QNetworkReply* m_reply{ nullptr };

        DownloadProgressCallback m_progress_cb;
        DownloadFinishedCallback m_finished_cb;

        QTimer m_speed_timer;       ///< Periodic speed/eta update (every 500ms)
        QElapsedTimer m_elapsed;    ///< Tracks time since download started (for speed calc)
        qint64 m_bytes_at_resume{}; ///< doneBytes at the start of current session (for speed calc)
    };

} // namespace disk::qml::transfers
