/**
 * @file UploadEngine.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief Chunked upload engine implementation
 *
 * @copyright Copyright (c) 2026
 */

#include "UploadEngine.hpp"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>

#include <api/ApiClient.hpp>
#include <dtos/ApiEnvelope.hpp>
#include <transfers/TransferQueueModel.hpp>

namespace disk::qml::transfers {

    // ==================== Construction / Destruction ====================

    UploadEngine::UploadEngine(
        const QString& filePath,
        quint64 parentId,
        api::ApiClient* apiClient,
        TransferQueueModel* queueModel,
        QObject* parent
    )
        : QObject(parent), m_file_path(filePath), m_parent_id(parentId), m_api_client(apiClient), m_queue_model(queueModel), m_file(filePath) {
        QFileInfo info(filePath);
        m_transfer_item = TransferItem::Create(
            TransferDirection::Upload,
            info.fileName(),
            info.size()
        );
    }

    UploadEngine::~UploadEngine() {
        if (m_file.isOpen()) {
            m_file.close();
        }
    }

    // ==================== Public API ====================

    void UploadEngine::Start() {
        if (m_in_flight) {
            return; // Already running
        }

        m_paused = false;
        m_cancelled = false;

        if (m_upload_id.isEmpty()) {
            // First start: compute file hash, then init
            ComputeFileHash();
        } else {
            // Resume: continue from next chunk
            m_speed_timer.restart();
            m_bytes_at_speed_start = m_transfer_item.doneBytes;
            SendNextChunk();
        }
    }

    void UploadEngine::Pause() {
        m_paused = true;
        if (!m_in_flight) {
            SetPaused();
        }
        // If in_flight, will pause after current chunk completes
    }

    void UploadEngine::Cancel() {
        m_cancelled = true;
        if (!m_in_flight && !m_upload_id.isEmpty()) {
            SendCancel();
        } else if (!m_in_flight) {
            // Never started init — just mark failed
            if (m_queue_model) {
                m_queue_model->UpdateStatus(
                    m_transfer_item.id,
                    TransferStatus::Failed,
                    QStringLiteral("Cancelled")
                );
            }
            emit finished(m_transfer_item.id, false, QStringLiteral("Cancelled"));
        }
        // If in_flight, will cancel after current request completes
    }

    auto UploadEngine::TransferId() const -> const QString& {
        return m_transfer_item.id;
    }

    // ==================== File Hash ====================

    void UploadEngine::ComputeFileHash() {
        if (!m_file.open(QIODevice::ReadOnly)) {
            SetFailed(QStringLiteral("Cannot open file: %1").arg(m_file.errorString()));
            return;
        }

        QCryptographicHash hasher(QCryptographicHash::Md5);
        constexpr qint64 kHashBufferSize = 8 * 1024 * 1024; // 8 MB read buffer for hashing

        while (!m_file.atEnd()) {
            const QByteArray buf = m_file.read(kHashBufferSize);
            if (buf.isEmpty()) {
                break;
            }
            hasher.addData(buf);
        }

        m_file_hash = QString::fromLatin1(hasher.result().toHex());
        m_file.close();

        // Start the upload flow
        SendInit();
    }

    // ==================== Init ====================

    void UploadEngine::SendInit() {
        if (m_cancelled) {
            if (m_queue_model) {
                m_queue_model->UpdateStatus(
                    m_transfer_item.id,
                    TransferStatus::Failed,
                    QStringLiteral("Cancelled")
                );
            }
            emit finished(m_transfer_item.id, false, QStringLiteral("Cancelled"));
            return;
        }

        QJsonObject body;
        body[QLatin1String("filename")] = m_transfer_item.fileName;
        body[QLatin1String("file_size")] = static_cast<qint64>(m_transfer_item.totalBytes);
        body[QLatin1String("file_hash")] = m_file_hash;
        body[QLatin1String("parent_id")] = static_cast<qint64>(m_parent_id);

        m_in_flight = true;
        auto* ctx = new QObject(this);
        m_api_client->PostJson(
            QStringLiteral("/api/file/upload/init"),
            body,
            ctx,
            [this, ctx](bool netErr, QString errStr, int status, QByteArray body) {
                ctx->deleteLater();
                m_in_flight = false;
                HandleInitResponse(netErr, errStr, status, body);
            }
        );
    }

    void UploadEngine::HandleInitResponse(
        bool netErr,
        const QString& errStr,
        int status,
        const QByteArray& body
    ) {
        if (m_cancelled) {
            // Cancelled while init was in flight
            if (!m_upload_id.isEmpty()) {
                SendCancel();
            } else {
                if (m_queue_model) {
                    m_queue_model->UpdateStatus(
                        m_transfer_item.id,
                        TransferStatus::Failed,
                        QStringLiteral("Cancelled")
                    );
                }
                emit finished(m_transfer_item.id, false, QStringLiteral("Cancelled"));
            }
            return;
        }

        if (netErr) {
            SetFailed(errStr.isEmpty() ? QStringLiteral("网络连接失败，请检查网络") : errStr);
            return;
        }

        auto envelope = models::ParseEnvelope(body);
        if (!envelope) {
            SetFailed(QStringLiteral("服务器响应解析失败"));
            return;
        }

        if (envelope->IsError()) {
            SetFailed(envelope->message);
            return;
        }

        auto dataObj = models::EnvelopeDataObject(*envelope);
        if (!dataObj) {
            SetFailed(QStringLiteral("服务器响应解析失败"));
            return;
        }

        const auto& data = *dataObj;

        // Check for instant upload (秒传)
        if (data.value(QLatin1String("instant_upload")).toBool(false)) {
            SetCompleted();
            return;
        }

        m_upload_id = data.value(QLatin1String("upload_id")).toString();
        m_chunk_size = static_cast<quint32>(data.value(QLatin1String("chunk_size")).toInt(static_cast<int>(kDefaultChunkSize)));
        m_total_chunks = static_cast<quint32>(data.value(QLatin1String("total_chunks")).toInt(0));

        if (m_upload_id.isEmpty() || m_total_chunks == 0) {
            SetFailed(QStringLiteral("服务器返回无效的上传参数"));
            return;
        }

        // Parse already-uploaded chunks (for resume)
        m_uploaded_chunks.clear();
        const auto chunksArray = data.value(QLatin1String("uploaded_chunks")).toArray();
        for (const auto& v : chunksArray) {
            m_uploaded_chunks.append(static_cast<quint32>(v.toInt()));
        }

        // Find the first chunk not yet uploaded
        m_next_chunk = 0;
        for (quint32 i = 0; i < m_total_chunks; ++i) {
            if (!m_uploaded_chunks.contains(i)) {
                m_next_chunk = i;
                break;
            }
        }

        // Calculate already-done bytes from uploaded chunks
        qint64 alreadyDone = static_cast<qint64>(m_uploaded_chunks.size()) * m_chunk_size;
        if (alreadyDone > m_transfer_item.totalBytes) {
            alreadyDone = m_transfer_item.totalBytes;
        }
        m_transfer_item.doneBytes = alreadyDone;
        UpdateProgress(alreadyDone);

        // Start speed tracking
        m_speed_timer.start();
        m_bytes_at_speed_start = alreadyDone;

        // Begin uploading chunks
        SendNextChunk();
    }

    // ==================== Chunk Upload ====================

    void UploadEngine::SendNextChunk() {
        if (m_cancelled) {
            SendCancel();
            return;
        }

        if (m_paused) {
            SetPaused();
            return;
        }

        // Find next chunk that hasn't been uploaded
        while (m_next_chunk < m_total_chunks && m_uploaded_chunks.contains(m_next_chunk)) {
            ++m_next_chunk;
        }

        if (m_next_chunk >= m_total_chunks) {
            // All chunks uploaded — send complete
            SendComplete();
            return;
        }

        auto [chunkData, chunkHash] = ReadChunk(m_next_chunk);
        if (chunkData.isEmpty() && chunkHash.isEmpty()) {
            // ReadChunk already called SetFailed
            return;
        }

        QUrlQuery query;
        query.addQueryItem(QStringLiteral("upload_id"), m_upload_id);
        query.addQueryItem(QStringLiteral("chunk_index"), QString::number(m_next_chunk));
        query.addQueryItem(QStringLiteral("chunk_hash"), chunkHash);

        const quint32 currentChunk = m_next_chunk;
        m_in_flight = true;
        auto* ctx = new QObject(this);
        m_api_client->PostRaw(
            QStringLiteral("/api/file/upload/chunk"),
            query,
            chunkData,
            ctx,
            [this, ctx, currentChunk](bool netErr, QString errStr, int status, QByteArray body) {
                ctx->deleteLater();
                m_in_flight = false;
                HandleChunkResponse(currentChunk, netErr, errStr, status, body);
            }
        );
    }

    auto UploadEngine::ReadChunk(quint32 chunkIndex) -> QPair<QByteArray, QString> {
        if (!m_file.isOpen()) {
            if (!m_file.open(QIODevice::ReadOnly)) {
                SetFailed(QStringLiteral("Cannot open file: %1").arg(m_file.errorString()));
                return {};
            }
        }

        const qint64 offset = static_cast<qint64>(chunkIndex) * m_chunk_size;
        if (!m_file.seek(offset)) {
            SetFailed(QStringLiteral("Cannot seek to chunk offset"));
            return {};
        }

        const QByteArray data = m_file.read(m_chunk_size);
        if (data.isEmpty()) {
            SetFailed(QStringLiteral("Failed to read chunk data"));
            return {};
        }

        // Compute chunk MD5 hash
        const QString hash = QString::fromLatin1(
            QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex()
        );

        return { data, hash };
    }

    void UploadEngine::HandleChunkResponse(
        quint32 chunkIndex,
        bool netErr,
        const QString& errStr,
        int status,
        const QByteArray& body
    ) {
        if (m_cancelled) {
            SendCancel();
            return;
        }

        if (netErr) {
            SetFailed(errStr.isEmpty() ? QStringLiteral("网络连接失败，请检查网络") : errStr);
            return;
        }

        auto envelope = models::ParseEnvelope(body);
        if (!envelope) {
            SetFailed(QStringLiteral("服务器响应解析失败"));
            return;
        }

        if (envelope->IsError()) {
            SetFailed(envelope->message);
            return;
        }

        // Mark chunk as uploaded
        m_uploaded_chunks.append(chunkIndex);
        ++m_next_chunk;

        // Update progress
        qint64 doneBytes = static_cast<qint64>(m_uploaded_chunks.size()) * m_chunk_size;
        if (doneBytes > m_transfer_item.totalBytes) {
            doneBytes = m_transfer_item.totalBytes;
        }
        m_transfer_item.doneBytes = doneBytes;
        UpdateProgress(doneBytes);

        if (m_paused) {
            SetPaused();
            return;
        }

        // Send next chunk
        SendNextChunk();
    }

    // ==================== Complete ====================

    void UploadEngine::SendComplete() {
        if (m_cancelled) {
            SendCancel();
            return;
        }

        QJsonObject body;
        body[QLatin1String("upload_id")] = m_upload_id;

        m_in_flight = true;
        auto* ctx = new QObject(this);
        m_api_client->PostJson(
            QStringLiteral("/api/file/upload/complete"),
            body,
            ctx,
            [this, ctx](bool netErr, QString errStr, int status, QByteArray body) {
                ctx->deleteLater();
                m_in_flight = false;
                HandleCompleteResponse(netErr, errStr, status, body);
            }
        );
    }

    void UploadEngine::HandleCompleteResponse(
        bool netErr,
        const QString& errStr,
        int status,
        const QByteArray& body
    ) {
        if (netErr) {
            SetFailed(errStr.isEmpty() ? QStringLiteral("网络连接失败，请检查网络") : errStr);
            return;
        }

        auto envelope = models::ParseEnvelope(body);
        if (!envelope) {
            SetFailed(QStringLiteral("服务器响应解析失败"));
            return;
        }

        if (envelope->IsError()) {
            SetFailed(envelope->message);
            return;
        }

        SetCompleted();
    }

    // ==================== Cancel ====================

    void UploadEngine::SendCancel() {
        if (m_upload_id.isEmpty()) {
            // No upload ID — nothing to cancel on backend
            if (m_queue_model) {
                m_queue_model->UpdateStatus(
                    m_transfer_item.id,
                    TransferStatus::Failed,
                    QStringLiteral("Cancelled")
                );
            }
            emit finished(m_transfer_item.id, false, QStringLiteral("Cancelled"));
            return;
        }

        m_in_flight = true;
        auto* ctx = new QObject(this);
        m_api_client->Delete(
            QStringLiteral("/api/file/upload/%1").arg(m_upload_id),
            ctx,
            [this, ctx](bool netErr, QString errStr, int status, QByteArray body) {
                ctx->deleteLater();
                m_in_flight = false;
                // Regardless of cancel result, mark as failed/cancelled
                if (m_queue_model) {
                    m_queue_model->UpdateStatus(
                        m_transfer_item.id,
                        TransferStatus::Failed,
                        QStringLiteral("Cancelled")
                    );
                }
                emit finished(m_transfer_item.id, false, QStringLiteral("Cancelled"));
            }
        );
    }

    // ==================== Progress / State Helpers ====================

    void UploadEngine::UpdateProgress(qint64 doneBytes) {
        if (!m_queue_model) {
            return;
        }

        qint64 speed = 0;
        qint64 eta = 0;

        if (m_speed_timer.isValid()) {
            const qint64 elapsedMs = m_speed_timer.elapsed();
            if (elapsedMs > 0) {
                const qint64 bytesDelta = doneBytes - m_bytes_at_speed_start;
                speed = bytesDelta * 1000 / elapsedMs; // bytes/sec
                if (speed > 0) {
                    const qint64 remaining = m_transfer_item.totalBytes - doneBytes;
                    eta = remaining / speed; // seconds
                }
            }
        }

        m_queue_model->UpdateProgress(m_transfer_item.id, doneBytes, speed, eta);
    }

    void UploadEngine::SetFailed(const QString& error) {
        if (m_file.isOpen()) {
            m_file.close();
        }

        if (m_queue_model) {
            m_queue_model->UpdateStatus(m_transfer_item.id, TransferStatus::Failed, error);
        }
        emit finished(m_transfer_item.id, false, error);
    }

    void UploadEngine::SetCompleted() {
        if (m_file.isOpen()) {
            m_file.close();
        }

        m_transfer_item.doneBytes = m_transfer_item.totalBytes;

        if (m_queue_model) {
            m_queue_model->UpdateProgress(m_transfer_item.id, m_transfer_item.totalBytes, 0, 0);
            m_queue_model->UpdateStatus(m_transfer_item.id, TransferStatus::Completed);
        }
        emit finished(m_transfer_item.id, true, QString{});
    }

    void UploadEngine::SetPaused() {
        if (m_file.isOpen()) {
            m_file.close();
        }

        if (m_queue_model) {
            m_queue_model->UpdateStatus(m_transfer_item.id, TransferStatus::Paused);
        }
    }

} // namespace disk::qml::transfers
