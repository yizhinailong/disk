/**
 * @file DownloadEngine.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 下载引擎: 通过 Range 请求流式下载到磁盘，支持暂停/取消/恢复
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 驱动单个文件下载:
 *  1. GET /api/file/download/{file_id}/info  → 获取 filename, size, hash, mime_type, supports_range
 *  2. GET /api/file/download/{file_id}       → 将 QNetworkReply 流式写入 .part 文件
 *
 * 支持:
 *  - 通过 Range: bytes=<offset>- 请求头恢复下载
 *  - 进度/速度/ETA 报告（通过 TransferItem 字段）
 *  - 暂停/取消操作
 *  - 成功后通过重命名 .part → 最终文件完成下载
 *
 * 不会将整个文件加载到内存 — 数据在到达时直接流式写入磁盘。
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
     * @brief 下载信息端点返回的元数据。
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
     * @brief 下载信息获取回调。
     * @param info  成功时包含信息；失败时为 std::nullopt。
     * @param error 人类可读的错误消息（成功时为空）。
     */
    using DownloadInfoCallback = std::function<void(std::optional<DownloadInfo> info, QString error)>;

    /**
     * @brief 下载进度更新回调。
     * @param item  当前 TransferItem 状态快照（doneBytes, speed, eta, status）。
     */
    using DownloadProgressCallback = std::function<void(const TransferItem& item)>;

    /**
     * @brief 下载完成时调用的回调（成功、失败或取消）。
     * @param item  最终 TransferItem 状态。
     */
    using DownloadFinishedCallback = std::function<void(const TransferItem& item)>;

    /**
     * @brief 下载引擎: 单文件流式下载到磁盘，支持 Range 请求和暂停/取消。
     *
     * @details
     * 工作流程:
     *  1. 调用 FetchInfo() 查询下载元数据。
     *  2. 调用 Start() 开始流式下载。
     *  3. 运行中可选调用 Pause() / Resume() / Cancel()。
     *  4. 成功后 .part 文件被重命名为最终文件名。
     *
     * 引擎在传输过程中写入 `<destDir>/<filename>.part`。
     * 成功完成后重命名为 `<destDir>/<filename>`。
     *
     * 一个 DownloadEngine 实例只处理一个下载。每个文件创建一个新实例。
     */
    class DownloadEngine : public QObject {
        Q_OBJECT

    public:
        /**
         * @brief 构造 DownloadEngine。
         *
         * @param client   共享的 ApiClient（必须比此引擎生命周期更长）。用于认证头和 NAM。
         * @param parent   QObject 父对象。
         */
        explicit DownloadEngine(api::ApiClient* client, QObject* parent = nullptr);
        ~DownloadEngine() override;

        /**
         * @brief 从 GET /api/file/download/{file_id}/info 获取下载元数据。
         *
         * @param fileId  后端文件 ID。
         * @param cb      获取完成后调用，传入解析后的信息或错误。
         */
        auto FetchInfo(qint64 fileId, DownloadInfoCallback cb) -> void;

        /**
         * @brief 获取分享文件的下载元数据。
         */
        auto FetchShareInfo(
            const QString& shareId,
            qint64 fileId,
            const QString& shareToken,
            DownloadInfoCallback cb
        ) -> void;

        /**
         * @brief 为分享文件准备下载。
         */
        auto PrepareForShare(
            const DownloadInfo& info,
            const QString& shareId,
            const QString& shareToken,
            const QString& destDir,
            DownloadProgressCallback progressCb,
            DownloadFinishedCallback finishedCb
        ) -> void;
        /**
         * @brief 准备下载（使用元数据），但不立即开始传输。
         *
         * @param info     之前通过 FetchInfo() 获取的下载元数据。
         * @param destDir  文件保存的本地目录。
         * @param progressCb  周期性调用，传入更新后的 TransferItem 状态。
         * @param finishedCb  下载完成、失败或取消时调用一次。
         *
         * 项目以 Queued 状态创建。调用 Begin() 或 Resume() 开始下载。
         */
        auto Prepare(
            const DownloadInfo& info,
            const QString& destDir,
            DownloadProgressCallback progressCb,
            DownloadFinishedCallback finishedCb
        ) -> void;

        /**
         * @brief 开始（或恢复）下载文件。
         *
         * @deprecated 请使用 Prepare() + Begin() 以支持队列感知流程。
         *
         * @param info     之前通过 FetchInfo() 获取的下载元数据。
         * @param destDir  文件保存的本地目录。
         * @param progressCb  周期性调用，传入更新后的 TransferItem 状态。
         * @param finishedCb  下载完成、失败或取消时调用一次。
         */
        auto Start(
            const DownloadInfo& info,
            const QString& destDir,
            DownloadProgressCallback progressCb,
            DownloadFinishedCallback finishedCb
        ) -> void;

        /**
         * @brief 暂停当前下载。可通过 Resume() 恢复。
         */
        auto Pause() -> void;

        /**
         * @brief 开始实际的网络传输。
         *
         * 必须在 Prepare() 之后调用。将状态设为 Running 并开始请求。
         */
        auto Begin() -> void;

        /**
         * @brief 恢复暂停或排队的下载。
         *
         * 同时处理 Paused（传输中暂停）和 Queued（已准备但未开始）状态。
         */
        auto Resume() -> void;

        /**
         * @brief 取消当前下载。删除 .part 文件。
         */
        auto Cancel() -> void;

        /**
         * @brief 获取当前传输项状态的只读快照。
         */
        [[nodiscard]] auto Item() const -> const TransferItem&;

        /// @brief 获取正在下载的后端文件 ID。
        [[nodiscard]] auto FileId() const -> qint64 { return m_info.fileId; }

        /// @brief 获取目标目录路径。
        [[nodiscard]] auto DestPath() const -> const QString& { return m_dest_dir; }

    signals:
        /**
         * @brief 当 TransferItem 状态变化时发出（进度、状态、速度、ETA）。
         */
        void itemChanged(const TransferItem& item);

    private:
        /// 从存储的信息初始化路径和项目（由 Prepare 调用）
        auto InitializeFromInfo() -> void;

        /// 启动网络请求（恢复时可选使用 Range 请求头）
        auto StartRequest() -> void;

        /// 完成: 重命名 .part → 最终文件，设置 status=Completed
        auto Finalize() -> void;

        /// 标记失败并设置错误消息
        auto Fail(const QString& error) -> void;

        /// 从计时器重新计算速度/ETA
        auto UpdateSpeedAndEta() -> void;

        /// 发出进度更新
        auto EmitProgress() -> void;

        // ----- Members -----

        api::ApiClient* m_client;

        TransferItem m_item;
        DownloadInfo m_info;
        QString m_dest_dir;
        QString m_share_id;
        QString m_share_token;
        bool m_is_share{ false };
        QString m_part_path;  ///< .part 文件的完整路径
        QString m_final_path; ///< 最终文件的完整路径

        QFile m_file;
        QNetworkReply* m_reply{ nullptr };

        DownloadProgressCallback m_progress_cb;
        DownloadFinishedCallback m_finished_cb;

        QTimer m_speed_timer;       ///< 周期性速度/ETA 更新（每 500ms）
        QElapsedTimer m_elapsed;    ///< 跟踪下载开始以来的时间（用于速度计算）
        qint64 m_bytes_at_resume{}; ///< 当前会话开始时的 doneBytes（用于速度计算）
    };

} // namespace disk::qml::transfers
