/**
 * @file DownloadEngine.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief DownloadEngine implementation — stream-to-disk with Range support
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "DownloadEngine.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>

#include <api/ApiClient.hpp>
#include <dtos/ApiEnvelope.hpp>

namespace disk::qml::transfers {

    namespace {

        /// Parse DownloadInfo from the envelope data object.
        auto ParseDownloadInfo(const QJsonObject& data) -> std::optional<DownloadInfo> {
            if (data.isEmpty()) {
                return std::nullopt;
            }

            DownloadInfo info;
            info.fileId = static_cast<qint64>(data.value("file_id").toDouble(0));
            info.filename = data.value("filename").toString();
            info.fileSize = static_cast<qint64>(data.value("file_size").toDouble(0));
            info.fileHash = data.value("file_hash").toString();
            info.mimeType = data.value("mime_type").toString();
            info.supportsRange = data.value("supports_range").toBool(true);

            if (info.filename.isEmpty() || info.fileSize <= 0) {
                return std::nullopt;
            }
            return info;
        }

        constexpr int kSpeedUpdateIntervalMs = 500;

    } // anonymous namespace

    // ==================== Construction ====================

    DownloadEngine::DownloadEngine(api::ApiClient* client, QObject* parent)
        : QObject(parent), m_client(client) {
        m_speed_timer.setInterval(kSpeedUpdateIntervalMs);
        connect(&m_speed_timer, &QTimer::timeout, this, &DownloadEngine::UpdateSpeedAndEta);
    }

    DownloadEngine::~DownloadEngine() {
        // Abort any in-flight reply
        if (m_reply) {
            m_reply->abort();
            m_reply->deleteLater();
            m_reply = nullptr;
        }
        m_speed_timer.stop();
        m_file.close();
    }

    // ==================== FetchInfo ====================

    auto DownloadEngine::FetchInfo(qint64 fileId, DownloadInfoCallback cb) -> void {
        auto* ctx = new QObject(this);

        m_client->Get(
            QStringLiteral("/api/file/download/%1/info").arg(fileId),
            ctx,
            [cb = std::move(cb), ctx](bool hasNetworkError, QString networkErrorString, int, QByteArray body) mutable {
                ctx->deleteLater();

                if (hasNetworkError) {
                    QString err = networkErrorString.isEmpty() ? QStringLiteral("网络连接失败，请检查网络") : networkErrorString;
                    cb(std::nullopt, err);
                    return;
                }

                auto envelope = models::ParseEnvelope(body);
                if (!envelope || !envelope->IsSuccess()) {
                    QString err = envelope ? envelope->message : QStringLiteral("服务器响应解析失败");
                    cb(std::nullopt, err);
                    return;
                }

                auto dataObj = models::EnvelopeDataObject(*envelope);
                if (!dataObj) {
                    cb(std::nullopt, QStringLiteral("下载信息解析失败"));
                    return;
                }
                auto info = ParseDownloadInfo(*dataObj);
                if (!info) {
                    cb(std::nullopt, QStringLiteral("下载信息解析失败"));
                    return;
                }

                cb(std::move(info), {});
            }
        );
    }

    // ==================== Start ====================

    auto DownloadEngine::Start(
        const DownloadInfo& info,
        const QString& destDir,
        DownloadProgressCallback progressCb,
        DownloadFinishedCallback finishedCb
    ) -> void {
        m_info = info;
        m_dest_dir = destDir;
        m_progress_cb = std::move(progressCb);
        m_finished_cb = std::move(finishedCb);

        // Build paths
        m_final_path = QDir(destDir).filePath(info.filename);
        m_part_path = m_final_path + QStringLiteral(".part");

        // Initialise the TransferItem
        m_item = TransferItem::Create(TransferDirection::Download, info.filename, info.fileSize);

        // Check if .part file exists (resume scenario)
        QFileInfo partInfo(m_part_path);
        if (partInfo.exists() && partInfo.size() > 0 && info.supportsRange) {
            m_item.doneBytes = partInfo.size();
        }

        m_item.status = TransferStatus::Running;
        m_bytes_at_resume = m_item.doneBytes;

        StartRequest();
    }

    // ==================== Pause ====================

    auto DownloadEngine::Pause() -> void {
        if (m_item.status != TransferStatus::Running) {
            return;
        }

        m_item.status = TransferStatus::Paused;
        m_item.speed = 0;
        m_item.eta = 0;
        m_speed_timer.stop();

        if (m_reply) {
            m_reply->abort();
            m_reply->deleteLater();
            m_reply = nullptr;
        }

        m_file.close();
        EmitProgress();
    }

    // ==================== Resume ====================

    auto DownloadEngine::Resume() -> void {
        if (m_item.status != TransferStatus::Paused) {
            return;
        }

        m_item.status = TransferStatus::Running;
        m_bytes_at_resume = m_item.doneBytes;

        StartRequest();
    }

    // ==================== Cancel ====================

    auto DownloadEngine::Cancel() -> void {
        m_speed_timer.stop();

        if (m_reply) {
            m_reply->abort();
            m_reply->deleteLater();
            m_reply = nullptr;
        }

        m_file.close();

        // Remove .part file
        QFile::remove(m_part_path);

        m_item.status = TransferStatus::Failed;
        m_item.error = QStringLiteral("已取消");
        m_item.speed = 0;
        m_item.eta = 0;

        EmitProgress();
        if (m_finished_cb) {
            m_finished_cb(m_item);
        }
    }

    // ==================== Item accessor ====================

    auto DownloadEngine::Item() const -> const TransferItem& {
        return m_item;
    }

    // ==================== Private: StartRequest ====================

    auto DownloadEngine::StartRequest() -> void {
        // Ensure dest directory exists
        QDir().mkpath(m_dest_dir);

        // Open .part file for append
        m_file.setFileName(m_part_path);
        QIODevice::OpenMode mode = QIODevice::WriteOnly;
        if (m_item.doneBytes > 0) {
            mode |= QIODevice::Append;
        }

        if (!m_file.open(mode)) {
            Fail(QStringLiteral("无法创建下载文件: %1").arg(m_file.errorString()));
            return;
        }

        // Build raw network request using ApiClient's streaming helper
        auto req = m_client->CreateStreamingRequest(
            QStringLiteral("/api/file/download/%1").arg(m_info.fileId)
        );

        // Set Range header for resume
        if (m_item.doneBytes > 0 && m_info.supportsRange) {
            req.setRawHeader(
                QByteArrayLiteral("Range"),
                QStringLiteral("bytes=%1-").arg(m_item.doneBytes).toUtf8()
            );
        }

        // Issue the GET via the raw NAM
        m_reply = m_client->NetworkAccessManager()->get(req);

        // Stream data as it arrives — do NOT buffer in memory
        connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
            if (!m_reply || m_item.status != TransferStatus::Running) {
                return;
            }
            const QByteArray chunk = m_reply->readAll();
            if (chunk.isEmpty()) {
                return;
            }
            m_file.write(chunk);
            m_item.doneBytes += chunk.size();
        });

        // Finished (success or error)
        connect(m_reply, &QNetworkReply::finished, this, [this]() {
            m_speed_timer.stop();

            if (!m_reply) {
                return;
            }

            // Paused or cancelled — reply was aborted, don't treat as error
            if (m_item.status == TransferStatus::Paused || m_item.status == TransferStatus::Failed) {
                m_reply->deleteLater();
                m_reply = nullptr;
                m_file.close();
                return;
            }

            const auto error = m_reply->error();
            const auto errorString = m_reply->errorString();
            const auto httpStatus = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            m_reply->deleteLater();
            m_reply = nullptr;
            m_file.close();

            if (error != QNetworkReply::NoError && error != QNetworkReply::OperationCanceledError) {
                Fail(QStringLiteral("网络错误: %1").arg(errorString));
                return;
            }

            // HTTP 200 (full) or 206 (partial content) are success
            if (httpStatus != 200 && httpStatus != 206 && httpStatus != 0) {
                Fail(QStringLiteral("服务器返回错误: HTTP %1").arg(httpStatus));
                return;
            }

            // Verify size match (if total known)
            if (m_item.totalBytes > 0 && m_item.doneBytes < m_item.totalBytes) {
                // Incomplete — but could be a connectivity issue
                Fail(QStringLiteral("下载不完整: %1/%2 字节").arg(m_item.doneBytes).arg(m_item.totalBytes));
                return;
            }

            Finalize();
        });

        // Start timers
        m_elapsed.start();
        m_speed_timer.start();
        EmitProgress();
    }

    // ==================== Private: Finalize ====================

    auto DownloadEngine::Finalize() -> void {
        // If final file already exists, remove it first
        if (QFile::exists(m_final_path)) {
            QFile::remove(m_final_path);
        }

        // Rename .part → final
        if (!QFile::rename(m_part_path, m_final_path)) {
            Fail(QStringLiteral("无法重命名下载文件"));
            return;
        }

        m_item.status = TransferStatus::Completed;
        m_item.speed = 0;
        m_item.eta = 0;
        m_item.doneBytes = m_item.totalBytes; // ensure 100%

        EmitProgress();
        if (m_finished_cb) {
            m_finished_cb(m_item);
        }
    }

    // ==================== Private: Fail ====================

    auto DownloadEngine::Fail(const QString& error) -> void {
        m_item.status = TransferStatus::Failed;
        m_item.error = error;
        m_item.speed = 0;
        m_item.eta = 0;

        EmitProgress();
        if (m_finished_cb) {
            m_finished_cb(m_item);
        }
    }

    // ==================== Private: UpdateSpeedAndEta ====================

    auto DownloadEngine::UpdateSpeedAndEta() -> void {
        if (m_item.status != TransferStatus::Running) {
            return;
        }

        const qint64 elapsedMs = m_elapsed.elapsed();
        if (elapsedMs <= 0) {
            return;
        }

        const qint64 bytesThisSession = m_item.doneBytes - m_bytes_at_resume;
        m_item.speed = bytesThisSession * 1000 / elapsedMs; // bytes per second

        if (m_item.speed > 0 && m_item.totalBytes > 0) {
            const qint64 remaining = m_item.totalBytes - m_item.doneBytes;
            m_item.eta = remaining / m_item.speed; // seconds
        } else {
            m_item.eta = 0;
        }

        EmitProgress();
    }

    // ==================== Private: EmitProgress ====================

    auto DownloadEngine::EmitProgress() -> void {
        emit itemChanged(m_item);
        if (m_progress_cb) {
            m_progress_cb(m_item);
        }
    }

} // namespace disk::qml::transfers
