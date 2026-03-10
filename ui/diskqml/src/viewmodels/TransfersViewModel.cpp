/**
 * @file TransfersViewModel.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief TransfersViewModel implementation
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "TransfersViewModel.hpp"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <api/ApiClient.hpp>
#include <dtos/ApiEnvelope.hpp>
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

    auto TransfersViewModel::Instance(void) -> TransfersViewModel* {
        return s_instance;
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

            // Add to model with Queued status - DrainUploadQueue will start when slot available
            m_upload_model->AddTransfer(engine->Item());

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
                return;
            }

            // Prepare the engine with metadata but don't start yet
            engine->Prepare(
                *info,
                destPath,

                [this, engine](const transfers::TransferItem& itemState) {
                    const QString transferId = engine->Item().id;
                    m_download_model->UpdateProgress(
                        transferId,
                        itemState.doneBytes,
                        itemState.speed,
                        itemState.eta
                    );
                    m_download_model->UpdateStatus(transferId, itemState.status, itemState.error);
                    UpdateCounts();
                },

                [this, engine](const transfers::TransferItem& itemState) {
                    const QString transferId = engine->Item().id;
                    OnDownloadFinished(transferId);
                }
            );

            // Add the engine's item (with Queued status) to the model
            const auto& item = engine->Item();
            m_download_model->AddTransfer(item);

            const QString transferId = item.id;
            m_download_engines.insert(transferId, engine);

            UpdateCounts();
            SaveState();

            // Let the queue drain policy decide when to start
            DrainDownloadQueue();
        });
    }

    void TransfersViewModel::startShareDownload(
        const QString& shareId,
        qint64 fileId,
        const QString& shareToken,
        const QString& destPath
    ) {
        if (shareToken.isEmpty() || fileId <= 0) {
            auto* ctx = new QObject(this);
            QJsonObject reqObj;
            reqObj.insert(QStringLiteral("password"), QStringLiteral(""));

            m_api_client->PostJson(QStringLiteral("/api/share/access/%1").arg(shareId), reqObj, ctx, [this, shareId, fileId, destPath, ctx](bool hasError, QString err, int, QByteArray body) {
                ctx->deleteLater();
                if (hasError) {
                    return;
                }
                auto env = disk::qml::models::ParseEnvelope(body);
                if (!env || !env->IsSuccess()) {
                    return;
                }
                auto data = disk::qml::models::EnvelopeDataObject(*env);
                if (!data) {
                    return;
                }
                QString token = data->value(QStringLiteral("share_token")).toString();
                if (token.isEmpty()) {
                    return;
                }

                if (fileId <= 0) {
                    auto* ctx2 = new QObject(this);
                    auto reqObj2 = m_api_client->CreateStreamingRequest(QStringLiteral("/api/share/browse/%1").arg(shareId));
                    reqObj2.setRawHeader(QByteArrayLiteral("X-Share-Token"), token.toUtf8());

                    auto* reply = m_api_client->NetworkAccessManager()->get(reqObj2);
                    connect(reply, &QNetworkReply::finished, ctx2, [this, shareId, token, destPath, ctx2, reply]() mutable {
                        ctx2->deleteLater();
                        reply->deleteLater();
                        if (reply->error() != QNetworkReply::NoError) {
                            return;
                        }
                        auto env2 = disk::qml::models::ParseEnvelope(reply->readAll());
                        if (!env2 || !env2->IsSuccess()) {
                            return;
                        }
                        auto data2 = disk::qml::models::EnvelopeDataObject(*env2);
                        if (!data2) {
                            return;
                        }
                        auto items = data2->value(QStringLiteral("items")).toArray();
                        for (const auto& val : items) {
                            auto obj = val.toObject();
                            qint64 id = static_cast<qint64>(obj.value(QStringLiteral("id")).toDouble(0));
                            if (id > 0 && obj.value(QStringLiteral("type")).toString() == QStringLiteral("file")) {
                                startShareDownload(shareId, id, token, destPath);
                            }
                        }
                    });
                } else {
                    startShareDownload(shareId, fileId, token, destPath);
                }
            });
            return;
        }

        auto* engine = new transfers::DownloadEngine(m_api_client, this);
        engine->FetchShareInfo(
            shareId,
            fileId,
            shareToken,
            [this, engine, shareId, shareToken, destPath](std::optional<transfers::DownloadInfo> info, QString error) {
                if (!info) {
                    engine->deleteLater();
                    return;
                }

                engine->PrepareForShare(
                    *info,
                    shareId,
                    shareToken,
                    destPath,
                    [this, engine](const transfers::TransferItem& itemState) {
                        const QString transferId = engine->Item().id;
                        m_download_model->UpdateProgress(
                            transferId,
                            itemState.doneBytes,
                            itemState.speed,
                            itemState.eta
                        );
                        m_download_model->UpdateStatus(transferId, itemState.status, itemState.error);
                        UpdateCounts();
                    },
                    [this, engine](const transfers::TransferItem& itemState) {
                        const QString transferId = engine->Item().id;
                        OnDownloadFinished(transferId);
                    }
                );

                const auto& item = engine->Item();
                m_download_model->AddTransfer(item);
                const QString transferId = item.id;
                m_download_engines.insert(transferId, engine);

                UpdateCounts();
                SaveState();
                DrainDownloadQueue();
            }
        );
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
            // Set status to Queued and let drain policy handle starting
            m_upload_model->UpdateStatus(id, transfers::TransferStatus::Queued);
            UpdateCounts();
            DrainUploadQueue();
            return;
        }
        if (auto* de = m_download_engines.value(id, nullptr)) {
            // Set status to Queued and let drain policy handle starting
            m_download_model->UpdateStatus(id, transfers::TransferStatus::Queued);
            UpdateCounts();
            DrainDownloadQueue();
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
        // --- Upload retry ---
        if (auto* oldEngine = m_upload_engines.value(id, nullptr)) {
            if (oldEngine->Item().status == transfers::TransferStatus::Failed) {
                // Extract original params before deleting
                const QString filePath = oldEngine->FilePath();
                const quint64 parentId = oldEngine->ParentId();

                // Remove old engine
                m_upload_model->RemoveTransfer(id);
                m_upload_engines.remove(id);
                oldEngine->deleteLater();

                // Create new engine with same params
                auto* newEngine = new transfers::UploadEngine(
                    filePath,
                    parentId,
                    m_api_client,
                    m_upload_model,
                    this
                );

                const QString newTransferId = newEngine->TransferId();
                m_upload_engines.insert(newTransferId, newEngine);

                // Add to model with Queued status
                m_upload_model->AddTransfer(newEngine->Item());

                // Connect finished signal
                connect(newEngine, &transfers::UploadEngine::finished, this, &TransfersViewModel::OnUploadFinished);

                UpdateCounts();
                DrainUploadQueue();
                SaveState();
                return;
            }
        }

        // --- Download retry ---
        if (auto* oldEngine = m_download_engines.value(id, nullptr)) {
            if (oldEngine->Item().status == transfers::TransferStatus::Failed) {
                // Extract original params before creating new engine
                const qint64 fileId = oldEngine->FileId();
                const QString destPath = oldEngine->DestPath();

                // Create new engine and fetch info (async)
                // Keep old item/engine until new one is ready to preserve Failed state on error
                auto* newEngine = new transfers::DownloadEngine(m_api_client, this);

                newEngine->FetchInfo(fileId, [this, oldEngine, newEngine, destPath, id](std::optional<transfers::DownloadInfo> info, QString error) {
                    if (!info) {
                        // FetchInfo failed - keep old Failed item, delete new engine
                        newEngine->deleteLater();
                        return;
                    }

                    // Prepare the engine with metadata but don't start yet
                    newEngine->Prepare(
                        *info,
                        destPath,

                        [this, newEngine](const transfers::TransferItem& itemState) {
                            const QString transferId = newEngine->Item().id;
                            m_download_model->UpdateProgress(
                                transferId,
                                itemState.doneBytes,
                                itemState.speed,
                                itemState.eta
                            );
                            m_download_model->UpdateStatus(transferId, itemState.status, itemState.error);
                            UpdateCounts();
                        },

                        [this, newEngine](const transfers::TransferItem& itemState) {
                            const QString transferId = newEngine->Item().id;
                            OnDownloadFinished(transferId);
                        }
                    );

                    // Now swap: remove old item/engine, add new one
                    m_download_model->RemoveTransfer(id);
                    m_download_engines.remove(id);
                    oldEngine->deleteLater();

                    // Add the new engine's item (with Queued status) to the model
                    const auto& item = newEngine->Item();
                    m_download_model->AddTransfer(item);

                    const QString transferId = item.id;
                    m_download_engines.insert(transferId, newEngine);

                    UpdateCounts();
                    SaveState();

                    // Let the queue drain policy decide when to start
                    DrainDownloadQueue();
                });

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

        // Don't call Start() directly - let DrainUploadQueue/DrainDownloadQueue handle it
        // since ResumeAll() already set status to Queued

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
                    // Update status to Running BEFORE starting, so if Start() fails immediately,
                    // SetFailed() will correctly update to Failed status
                    m_upload_model->UpdateStatus(item.id, transfers::TransferStatus::Running);
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
