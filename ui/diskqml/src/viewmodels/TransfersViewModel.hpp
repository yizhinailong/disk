/**
 * @file TransfersViewModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QML ViewModel for upload/download transfer management
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Owns two TransferQueueModels (uploads, downloads) and drives
 * UploadEngine / DownloadEngine instances for each active transfer.
 * Respects concurrency limits from ConfigStore.
 *
 * QML layer binds to properties and invokes Q_INVOKABLE methods.
 * All business logic stays in C++.
 */

#pragma once

#include <QHash>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QUrl>
#include <QVector>

#include <QtQml/qjsengine.h>
#include <QtQml/qqmlengine.h>
#include <QtQml/qqmlregistration.h>

namespace disk::qml::api {
    class ApiClient;
} // namespace disk::qml::api

namespace disk::qml::transfers {
    class TransferQueueModel;
    class TransferStore;
    class UploadEngine;
    class DownloadEngine;
} // namespace disk::qml::transfers

namespace disk::qml::utils {
    class ConfigStore;
} // namespace disk::qml::utils

namespace disk::qml::viewmodels {

    /**
     * @brief QML ViewModel that manages upload and download transfer queues.
     *
     * @details
     * - Owns separate TransferQueueModel for uploads and downloads.
     * - Creates UploadEngine/DownloadEngine per transfer, respecting concurrency limits.
     * - Persists queue state via TransferStore.
     * - Exposes counts and models for QML binding.
     */
    class TransfersViewModel : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_SINGLETON

        // ==================== Models ====================

        /// Upload queue model for QML ListView binding.
        Q_PROPERTY(disk::qml::transfers::TransferQueueModel* uploadModel READ UploadModel CONSTANT)
        /// Download queue model for QML ListView binding.
        Q_PROPERTY(disk::qml::transfers::TransferQueueModel* downloadModel READ DownloadModel CONSTANT)

        // ==================== Counts ====================

        /// Number of active (Running) uploads.
        Q_PROPERTY(int activeUploadCount READ ActiveUploadCount NOTIFY activeUploadCountChanged)
        /// Number of active (Running) downloads.
        Q_PROPERTY(int activeDownloadCount READ ActiveDownloadCount NOTIFY activeDownloadCountChanged)
        /// Total uploads in queue (all statuses).
        Q_PROPERTY(int totalUploadCount READ TotalUploadCount NOTIFY totalUploadCountChanged)
        /// Total downloads in queue (all statuses).
        Q_PROPERTY(int totalDownloadCount READ TotalDownloadCount NOTIFY totalDownloadCountChanged)

    public:
        explicit TransfersViewModel(
            api::ApiClient* apiClient,
            transfers::TransferStore* store,
            utils::ConfigStore* configStore,
            QObject* parent = nullptr
        );

        ~TransfersViewModel() override;

        // ==================== Singleton ====================

        static auto SetInstance(TransfersViewModel* instance) -> void;
        static auto Instance() -> TransfersViewModel*;
        static auto create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> TransfersViewModel*;

        // ==================== Property Getters ====================

        [[nodiscard]] auto UploadModel() const -> transfers::TransferQueueModel*;
        [[nodiscard]] auto DownloadModel() const -> transfers::TransferQueueModel*;
        [[nodiscard]] auto ActiveUploadCount() const -> int;
        [[nodiscard]] auto ActiveDownloadCount() const -> int;
        [[nodiscard]] auto TotalUploadCount() const -> int;
        [[nodiscard]] auto TotalDownloadCount() const -> int;

        // ==================== Upload Operations ====================

        /// Start uploading local files to a target folder.
        /// @param fileUrls List of local file URLs (from QML FileDialog).
        /// @param targetFolderId Backend parent folder ID (0 = root).
        Q_INVOKABLE void startUpload(const QList<QUrl>& fileUrls, qint64 targetFolderId);

        // ==================== Download Operations ====================

        /// Start downloading a file from the backend.
        /// @param fileId Backend file ID.
        /// @param destPath Local destination directory path.
        Q_INVOKABLE void startDownload(qint64 fileId, const QString& destPath);

        /// Start downloading a shared file (uses share token instead of JWT)
        Q_INVOKABLE void startShareDownload(
            const QString& shareId,
            qint64 fileId,
            const QString& shareToken,
            const QString& destPath
        );
        // ==================== Transfer Control ====================

        /// Pause a specific transfer (upload or download) by its UUID.
        Q_INVOKABLE void pauseTransfer(const QString& id);

        /// Resume a specific transfer (upload or download) by its UUID.
        Q_INVOKABLE void resumeTransfer(const QString& id);

        /// Cancel a specific transfer (upload or download) by its UUID.
        Q_INVOKABLE void cancelTransfer(const QString& id);

        /// Retry a failed transfer by its UUID.
        Q_INVOKABLE void retryTransfer(const QString& id);

        /// Remove all completed transfers from both queues.
        Q_INVOKABLE void clearCompleted();

        /// Pause all running transfers (uploads and downloads).
        Q_INVOKABLE void pauseAll();

        /// Resume all paused transfers (uploads and downloads).
        Q_INVOKABLE void resumeAll();

    signals:
        void activeUploadCountChanged();
        void activeDownloadCountChanged();
        void totalUploadCountChanged();
        void totalDownloadCountChanged();

    private:
        // ==================== Engine Management ====================

        /// Try to start queued uploads up to the concurrency limit.
        auto DrainUploadQueue() -> void;

        /// Try to start queued downloads up to the concurrency limit.
        auto DrainDownloadQueue() -> void;

        /// Handle an upload engine finishing (success, failure, or cancel).
        auto OnUploadFinished(const QString& transferId, bool success, const QString& error) -> void;

        /// Handle a download engine finishing.
        auto OnDownloadFinished(const QString& transferId) -> void;

        /// Persist current queue state to disk.
        auto SaveState() -> void;

        /// Rehydrate queue state from disk on startup.
        auto LoadState() -> void;

        /// Recalculate active counts and emit signals if changed.
        auto UpdateCounts() -> void;

        // ==================== State ====================

        api::ApiClient* m_api_client;
        transfers::TransferStore* m_store;
        utils::ConfigStore* m_config_store;

        transfers::TransferQueueModel* m_upload_model;
        transfers::TransferQueueModel* m_download_model;

        /// Active upload engines keyed by transfer UUID.
        QHash<QString, transfers::UploadEngine*> m_upload_engines;
        /// Active download engines keyed by transfer UUID.
        QHash<QString, transfers::DownloadEngine*> m_download_engines;

        int m_active_upload_count{ 0 };
        int m_active_download_count{ 0 };
        int m_total_upload_count{ 0 };
        int m_total_download_count{ 0 };

        inline static TransfersViewModel* s_instance = nullptr;
        inline static QJSEngine* s_engine = nullptr;
    };

} // namespace disk::qml::viewmodels
