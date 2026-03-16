/**
 * @file DownloadEngine.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief DownloadEngine 实现 — 支持 Range 请求的流式下载
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "DownloadEngine.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrlQuery>

#include <api/ApiClient.hpp>
#include <dtos/ApiEnvelope.hpp>

namespace disk::qml::transfers {

    namespace {

        /// 从 envelope data 对象解析 DownloadInfo。
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

    // ==================== 构造函数 ====================

    DownloadEngine::DownloadEngine(api::ApiClient* client, QObject* parent)
        : QObject(parent), m_client(client) {
        m_speed_timer.setInterval(kSpeedUpdateIntervalMs);
        connect(&m_speed_timer, &QTimer::timeout, this, &DownloadEngine::UpdateSpeedAndEta);
    }

    DownloadEngine::~DownloadEngine() {
        // 中止任何进行中的 reply
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

    auto DownloadEngine::FetchShareInfo(
        const QString& shareId,
        qint64 fileId,
        const QString& shareToken,
        DownloadInfoCallback cb
    ) -> void {
        auto* ctx = new QObject(this);

        QString path = QStringLiteral("/api/share/browse/%1").arg(shareId);
        auto req = m_client->CreateStreamingRequest(path);

        QUrl url = req.url();
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("file_id"), QString::number(fileId));
        url.setQuery(query);
        req.setUrl(url);

        req.setRawHeader(QByteArrayLiteral("X-Share-Token"), shareToken.toUtf8());

        auto* nam = m_client->NetworkAccessManager();
        auto* reply = nam->get(req);

        connect(reply, &QNetworkReply::finished, ctx, [reply, cb = std::move(cb), ctx, fileId]() mutable {
            ctx->deleteLater();
            reply->deleteLater();

            bool hasError = reply->error() != QNetworkReply::NoError;
            QString err = hasError ? reply->errorString() : QString{};
            QByteArray body = reply->readAll();

            if (hasError) {
                cb(std::nullopt, err.isEmpty() ? QStringLiteral("网络错误") : err);
                return;
            }
            auto env = models::ParseEnvelope(body);
            if (!env || !env->IsSuccess()) {
                cb(std::nullopt, env ? env->message : QStringLiteral("获取失败"));
                return;
            }
            auto dataObj = models::EnvelopeDataObject(*env);
            DownloadInfo info;
            info.fileId = fileId;
            info.filename = QStringLiteral("share_%1").arg(fileId);
            info.fileSize = 0;
            info.supportsRange = true;
            if (dataObj) {
                auto items = dataObj->value(QStringLiteral("items")).toArray();
                for (const auto& v : items) {
                    auto item = v.toObject();
                    if (static_cast<qint64>(item.value(QStringLiteral("id")).toDouble(0)) == fileId || items.size() == 1) {
                        info.filename = item.value(QStringLiteral("name")).toString(info.filename);
                        info.fileSize = static_cast<qint64>(item.value(QStringLiteral("size")).toDouble(0));
                        break;
                    }
                }
            }
            cb(std::move(info), {});
        });
    }

    auto DownloadEngine::PrepareForShare(
        const DownloadInfo& info,
        const QString& shareId,
        const QString& shareToken,
        const QString& destDir,
        DownloadProgressCallback progressCb,
        DownloadFinishedCallback finishedCb
    ) -> void {
        m_is_share = true;
        m_share_id = shareId;
        m_share_token = shareToken;
        Prepare(info, destDir, std::move(progressCb), std::move(finishedCb));
    }

    // ==================== Prepare ====================

    auto DownloadEngine::Prepare(
        const DownloadInfo& info,
        const QString& destDir,
        DownloadProgressCallback progressCb,
        DownloadFinishedCallback finishedCb
    ) -> void {
        m_info = info;
        m_dest_dir = destDir;
        m_progress_cb = std::move(progressCb);
        m_finished_cb = std::move(finishedCb);

        InitializeFromInfo();

        // 保持状态为 Queued — 尚未启动
        EmitProgress();
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

        InitializeFromInfo();

        // 旧行为：立即启动
        Begin();
    }

    // ==================== Begin ====================

    auto DownloadEngine::Begin() -> void {
        if (m_item.status != TransferStatus::Queued) {
            return;
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
        // 处理 Queued（已准备但未启动）— 开始新传输
        if (m_item.status == TransferStatus::Queued) {
            Begin();
            return;
        }

        // 处理 Paused（传输中途暂停）— 从上次位置继续
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

        // 移除 .part 文件
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

    // ==================== Private: InitializeFromInfo ====================

    auto DownloadEngine::InitializeFromInfo() -> void {
        // 构建路径
        m_final_path = QDir(m_dest_dir).filePath(m_info.filename);
        m_part_path = m_final_path + QStringLiteral(".part");

        // 初始化 TransferItem
        m_item = TransferItem::Create(TransferDirection::Download, m_info.filename, m_info.fileSize);

        // 检查 .part 文件是否存在（断点续传场景）
        QFileInfo partInfo(m_part_path);
        if (partInfo.exists() && partInfo.size() > 0 && m_info.supportsRange) {
            m_item.doneBytes = partInfo.size();
        }

        // 状态保持 Queued 直到调用 Begin()
    }

    // ==================== Private: StartRequest ====================

    auto DownloadEngine::StartRequest() -> void {
        // 确保目标目录存在
        QDir().mkpath(m_dest_dir);

        // 以追加模式打开 .part 文件
        m_file.setFileName(m_part_path);
        QIODevice::OpenMode mode = QIODevice::WriteOnly;
        if (m_item.doneBytes > 0) {
            mode |= QIODevice::Append;
        }

        if (!m_file.open(mode)) {
            Fail(QStringLiteral("无法创建下载文件: %1").arg(m_file.errorString()));
            return;
        }

        // 使用 ApiClient 的流式辅助函数构建原始网络请求
        QString endpoint = m_is_share ? QStringLiteral("/api/share/download/%1/%2").arg(m_share_id).arg(m_info.fileId) : QStringLiteral("/api/file/download/%1").arg(m_info.fileId);

        auto req = m_client->CreateStreamingRequest(endpoint);

        if (m_is_share && !m_share_token.isEmpty()) {
            req.setRawHeader(QByteArrayLiteral("X-Share-Token"), m_share_token.toUtf8());
        }

        // 设置 Range 头用于断点续传
        if (m_item.doneBytes > 0 && m_info.supportsRange) {
            req.setRawHeader(
                QByteArrayLiteral("Range"),
                QStringLiteral("bytes=%1-").arg(m_item.doneBytes).toUtf8()
            );
        }

        // 通过原始 NAM 发起 GET 请求
        m_reply = m_client->NetworkAccessManager()->get(req);

        // 数据到达时流式写入 — 不在内存中缓冲
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

        // 完成（成功或错误）
        connect(m_reply, &QNetworkReply::finished, this, [this]() {
            m_speed_timer.stop();

            if (!m_reply) {
                return;
            }

            // 已暂停或已取消 — reply 已中止，不视为错误
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

            // HTTP 200（完整）或 206（部分内容）为成功
            if (httpStatus != 200 && httpStatus != 206 && httpStatus != 0) {
                Fail(QStringLiteral("服务器返回错误: HTTP %1").arg(httpStatus));
                return;
            }

            // 验证大小匹配（如果总数已知）
            if (m_item.totalBytes > 0 && m_item.doneBytes < m_item.totalBytes) {
                // 不完整 — 但可能是连接问题
                Fail(QStringLiteral("下载不完整: %1/%2 字节").arg(m_item.doneBytes).arg(m_item.totalBytes));
                return;
            }

            Finalize();
        });

        // 启动计时器
        m_elapsed.start();
        m_speed_timer.start();
        EmitProgress();
    }

    // ==================== Private: Finalize ====================

    auto DownloadEngine::Finalize() -> void {
        // 如果最终文件已存在，先移除它
        if (QFile::exists(m_final_path)) {
            QFile::remove(m_final_path);
        }

        // 重命名 .part → 最终文件
        if (!QFile::rename(m_part_path, m_final_path)) {
            Fail(QStringLiteral("无法重命名下载文件"));
            return;
        }

        m_item.status = TransferStatus::Completed;
        m_item.speed = 0;
        m_item.eta = 0;
        m_item.doneBytes = m_item.totalBytes; // 确保 100%

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
        m_item.speed = bytesThisSession * 1000 / elapsedMs; // 字节/秒

        if (m_item.speed > 0 && m_item.totalBytes > 0) {
            const qint64 remaining = m_item.totalBytes - m_item.doneBytes;
            m_item.eta = remaining / m_item.speed; // 秒
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
