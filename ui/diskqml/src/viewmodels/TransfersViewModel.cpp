/**
 * @file TransfersViewModel.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief TransfersViewModel implementation
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "TransfersViewModel.hpp"

#include <QFileInfo>
#include <QUrl>

#include <api/ApiClient.hpp>
#include <transfers/DownloadEngine.hpp>
#include <transfers/TransferItem.hpp>
#include <transfers/TransferQueueModel.hpp>
#include <transfers/TransferStore.hpp>
#include <transfers/UploadEngine.hpp>
#include <utils/ConfigStore.hpp>

namespace disk::qml::viewmodels {

    // ==================== Constructor / Destructor ====================

    TransfersViewModel::TransfersViewModel(
        api::ApiClient* apiClient,
        transfers::TransferStore* store,
        utils::ConfigStore* configStore,
        QObject* parent
    ) : QObject(parent),
        m_api_client(apiClient),
        m_store(store),
        m_config_store(configStore),
        m_upload_model(new transfers::TransferQueueModel(this)),
        m_download_model(new transfers::TransferQueueModel(this)) {
        LoadState();
    }

    TransfersViewModel::~TransfersViewModel() {
        SaveState();


        qDeleteAll(m_upload_engines);
        m_upload_engines.clear();
        qDeleteAll(m_download_engines);
        m_download_engines.clear();
    }

    // ==================== Singleton ====================

    auto TransfersViewModel::SetInstance(TransfersViewModel* instance) -> void {
        s_instance = instance;
    }

    auto TransfersViewModel::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> TransfersViewModel* {
        Q_ASSERT(s_instance);
        Q_ASSERT(!s_engine || s_engine == jsEngine);
        s_engine = jsEngine;

        QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
        return s_instance;
    }

    // ==================== Property Getters ====================

    auto TransfersViewModel::UploadModel() const -> transfers::TransferQueueModel* {
        return m_upload_model;
    }

    auto TransfersViewModel::DownloadModel() const -> transfers::TransferQueueModel* {
        return m_download_model;
    }

    auto TransfersViewModel::ActiveUploadCount() const -> int {
        return m_active_upload_count;
    }

    auto TransfersViewModel::ActiveDownloadCount() const -> int {
        return m_active_download_count;
    }

    auto TransfersViewModel::TotalUploadCount() const -> int {
        return m_total_upload_count;
    }

    auto TransfersViewModel::TotalDownloadCount() const -> int {
        return m_total_download_count;
    }

    // ==================== Upload Operations ====================

    void TransfersViewModel::startUpload(const QList<QUrl>& fileUrls, qint64 targetFolderId) {
        for (const auto& url : fileUrls) {
            const QString filePath = url.toLocalFile();
            if (filePath.isEmpty()) {
                continue;
            }

            auto* engine = new transfers::UploadEngine(
                filePath,
                static_cast<quint64>(targetFolderId),
                m_api_client,
                m_upload_model,
                this
            );

            const QString transferId = engine->TransferId();
            m_upload_engines.insert(transferId, engine);

            connect(engine, &transfers::UploadEngine::finished, this, &TransfersViewModel::OnUploadFinished);
        }

        UpdateCounts();
        DrainUploadQueue();
        SaveState();
    }

    // ==================== Download Operations ====================

    void TransfersViewModel::startDownload(qint64 fileId, const QString& destPath) {
        auto* engine = new transfers::DownloadEngine(m_api_client, this);


        engine->FetchInfo(fileId, [this, engine, destPath](std::optional<transfers::DownloadInfo> info, QString error) {
            if (!info) {
                engine->deleteLater();
                engine->deleteLater();
                return;
            }


            auto item = transfers::TransferItem::Create(
                transfers::TransferDirection::Download,
                info->filename,
                info->fileSize
            );
            m_download_model->AddTransfer(item);

            const QString transferId = item.id;
            m_download_engines.insert(transferId, engine);


            engine->Start(
                *info,
                destPath,

                [this, transferId](const transfers::TransferItem& itemState) {
                    m_download_model->UpdateProgress(
                        transferId,
                        itemState.doneBytes,
                        itemState.speed,
                        itemState.eta
                    );
                    m_download_model->UpdateStatus(transferId, itemState.status, itemState.error);
                    UpdateCounts();
                },

                [this, transferId](const transfers::TransferItem& itemState) {
                    OnDownloadFinished(transferId);
                }
            );

            UpdateCounts();
            SaveState();
        });
    }

    // ==================== Transfer Control ====================

    void TransfersViewModel::pauseTransfer(const QString& id) {
        if (auto* ue = m_upload_engines.value(id, nullptr)) {
            ue->Pause();
            UpdateCounts();
            SaveState();
            return;
        }
        if (auto* de = m_download_engines.value(id, nullptr)) {
            de->Pause();
            m_download_model->UpdateStatus(id, transfers::TransferStatus::Paused);
            UpdateCounts();
            SaveState();
            return;
        }
    }

    void TransfersViewModel::resumeTransfer(const QString& id) {
        if (auto* ue = m_upload_engines.value(id, nullptr)) {
            ue->Start();
            UpdateCounts();
            return;
        }
        if (auto* de = m_download_engines.value(id, nullptr)) {
            de->Resume();
            m_download_model->UpdateStatus(id, transfers::TransferStatus::Running);
            UpdateCounts();
            return;
        }
    }

    void TransfersViewModel::cancelTransfer(const QString& id) {
        if (auto* ue = m_upload_engines.value(id, nullptr)) {
            ue->Cancel();

            return;
        }
        if (auto* de = m_download_engines.value(id, nullptr)) {
            de->Cancel();

            return;
        }

        m_upload_model->RemoveTransfer(id);
        m_download_model->RemoveTransfer(id);
        UpdateCounts();
        SaveState();
    }

    void TransfersViewModel::retryTransfer(const QString& id) {

        const auto& uploadItems = m_upload_model->Items();
        for (const auto& item : uploadItems) {
            if (item.id == id && item.status == transfers::TransferStatus::Failed) {

                m_upload_model->RemoveTransfer(id);
                if (auto* oldEngine = m_upload_engines.take(id)) {
                    oldEngine->deleteLater();
                }

                UpdateCounts();
                SaveState();
                return;
            }
        }


        const auto& downloadItems = m_download_model->Items();
        for (const auto& item : downloadItems) {
            if (item.id == id && item.status == transfers::TransferStatus::Failed) {
                m_download_model->RemoveTransfer(id);
                if (auto* oldEngine = m_download_engines.take(id)) {
                    oldEngine->deleteLater();
                }
                UpdateCounts();
                SaveState();
                return;
            }
        }
    }

    void TransfersViewModel::clearCompleted() {
        m_upload_model->ClearCompleted();
        m_download_model->ClearCompleted();
        UpdateCounts();
        SaveState();
    }

    void TransfersViewModel::pauseAll() {
        m_upload_model->PauseAll();
        m_download_model->PauseAll();


        for (auto* engine : m_upload_engines) {
            engine->Pause();
        }

        for (auto* engine : m_download_engines) {
            engine->Pause();
        }

        UpdateCounts();
        SaveState();
    }

    void TransfersViewModel::resumeAll() {
        m_upload_model->ResumeAll();
        m_download_model->ResumeAll();


        for (auto* engine : m_upload_engines) {
            engine->Start();
        }

        for (auto* engine : m_download_engines) {
            engine->Resume();
        }

        UpdateCounts();
        DrainUploadQueue();
        DrainDownloadQueue();
    }

    // ==================== Engine Management ====================

    auto TransfersViewModel::DrainUploadQueue() -> void {
        const int maxConcurrent = m_config_store->ConcurrentUploads();
        int running = 0;


        for (const auto& item : m_upload_model->Items()) {
            if (item.status == transfers::TransferStatus::Running) {
                ++running;
            }
        }


        for (const auto& item : m_upload_model->Items()) {
            if (running >= maxConcurrent) {
                break;
            }
            if (item.status == transfers::TransferStatus::Queued) {
                if (auto* engine = m_upload_engines.value(item.id, nullptr)) {
                    engine->Start();
                    ++running;
                }
            }
        }
    }

    auto TransfersViewModel::DrainDownloadQueue() -> void {
        const int maxConcurrent = m_config_store->ConcurrentDownloads();
        int running = 0;

        for (const auto& item : m_download_model->Items()) {
            if (item.status == transfers::TransferStatus::Running) {
                ++running;
            }
        }

        for (const auto& item : m_download_model->Items()) {
            if (running >= maxConcurrent) {
                break;
            }
            if (item.status == transfers::TransferStatus::Queued) {
                if (auto* engine = m_download_engines.value(item.id, nullptr)) {
                    engine->Resume();
                    ++running;
                }
            }
        }
    }

    auto TransfersViewModel::OnUploadFinished(
        const QString& transferId,
        bool success,
        const QString& error
    ) -> void {

        if (auto* engine = m_upload_engines.take(transferId)) {
            engine->deleteLater();
        }

        UpdateCounts();
        SaveState();


        DrainUploadQueue();
    }

    auto TransfersViewModel::OnDownloadFinished(const QString& transferId) -> void {

        if (auto* engine = m_download_engines.take(transferId)) {
            engine->deleteLater();
        }

        UpdateCounts();
        SaveState();


        DrainDownloadQueue();
    }

    auto TransfersViewModel::SaveState() -> void {
        if (m_store) {
            m_store->Save(m_upload_model->Items(), m_download_model->Items());
        }
    }

    auto TransfersViewModel::LoadState() -> void {
        if (!m_store) {
            return;
        }

        QVector<transfers::TransferItem> uploads;
        QVector<transfers::TransferItem> downloads;

        if (m_store->Load(uploads, downloads)) {
            m_upload_model->ResetItems(uploads);
            m_download_model->ResetItems(downloads);
            UpdateCounts();
        }
    }

    auto TransfersViewModel::UpdateCounts() -> void {
        int activeUploads = 0;
        int activeDownloads = 0;

        for (const auto& item : m_upload_model->Items()) {
            if (item.status == transfers::TransferStatus::Running) {
                ++activeUploads;
            }
        }

        for (const auto& item : m_download_model->Items()) {
            if (item.status == transfers::TransferStatus::Running) {
                ++activeDownloads;
            }
        }

        const int totalUploads = m_upload_model->Count();
        const int totalDownloads = m_download_model->Count();

        if (m_active_upload_count != activeUploads) {
            m_active_upload_count = activeUploads;
            emit activeUploadCountChanged();
        }
        if (m_active_download_count != activeDownloads) {
            m_active_download_count = activeDownloads;
            emit activeDownloadCountChanged();
        }
        if (m_total_upload_count != totalUploads) {
            m_total_upload_count = totalUploads;
            emit totalUploadCountChanged();
        }
        if (m_total_download_count != totalDownloads) {
            m_total_download_count = totalDownloads;
            emit totalDownloadCountChanged();
        }
    }

} // namespace disk::qml::viewmodels
