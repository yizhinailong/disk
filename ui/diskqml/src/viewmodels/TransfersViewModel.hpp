/**
 * @file TransfersViewModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 传输视图模型，管理上传/下载传输队列
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 拥有两个 TransferQueueModel（上传、下载）并为每个活动传输
 * 驱动 UploadEngine / DownloadEngine 实例。
 * 遵守 ConfigStore 的并发限制。
 *
 * QML 层绑定属性并调用 Q_INVOKABLE 方法。
 * 所有业务逻辑都在 C++ 中。
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
     * @brief 管理上传和下载传输队列的 QML 视图模型。
     *
     * @details
     * - 拥有独立的上传和下载 TransferQueueModel。
     * - 为每个传输创建 UploadEngine/DownloadEngine，遵守并发限制。
     * - 通过 TransferStore 持久化队列状态。
     * - 向 QML 暴露计数和模型以供绑定。
     *
     * 单例边界审计（任务 7）：应用全局。传输队列和运行中的引擎
     * 必须在页面切换后存活，并可从多个视图观察，
     * 因此保持为类型化 QML 单例。
     */
    class TransfersViewModel : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_SINGLETON

        // ==================== 模型 ====================

        Q_PROPERTY(disk::qml::transfers::TransferQueueModel* uploadModel READ UploadModel CONSTANT)     ///< QML ListView 绑定用的上传队列模型
        Q_PROPERTY(disk::qml::transfers::TransferQueueModel* downloadModel READ DownloadModel CONSTANT) ///< QML ListView 绑定用的下载队列模型

        // ==================== 计数 ====================

        Q_PROPERTY(int activeUploadCount READ ActiveUploadCount NOTIFY activeUploadCountChanged)       ///< 活动（运行中）的上传数
        Q_PROPERTY(int activeDownloadCount READ ActiveDownloadCount NOTIFY activeDownloadCountChanged) ///< 活动（运行中）的下载数
        Q_PROPERTY(int totalUploadCount READ TotalUploadCount NOTIFY totalUploadCountChanged)          ///< 队列中的总上传数（所有状态）
        Q_PROPERTY(int totalDownloadCount READ TotalDownloadCount NOTIFY totalDownloadCountChanged)    ///< 队列中的总下载数（所有状态）

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

        // ==================== 上传操作 ====================

        /// 开始上传本地文件到目标文件夹。
        /// @param fileUrls 本地文件 URL 列表（来自 QML FileDialog）。
        /// @param targetFolderId 后端父文件夹 ID（0 = 根目录）。
        Q_INVOKABLE void startUpload(const QList<QUrl>& fileUrls, qint64 targetFolderId);

        // ==================== 下载操作 ====================

        /// 从后端开始下载文件。
        /// @param fileId 后端文件 ID。
        /// @param destPath 本地目标目录路径。
        Q_INVOKABLE void startDownload(qint64 fileId, const QString& destPath);

        Q_INVOKABLE void startShareDownload(
            const QString& shareId,
            qint64 fileId,
            const QString& shareToken,
            const QString& destPath
        ); ///< 开始下载分享文件（使用分享令牌而非 JWT）
        // ==================== 传输控制 ====================

        Q_INVOKABLE void pauseTransfer(const QString& id);  ///< 暂停指定传输（上传或下载）

        Q_INVOKABLE void resumeTransfer(const QString& id); ///< 恢复指定传输（上传或下载）

        Q_INVOKABLE void cancelTransfer(const QString& id); ///< 取消指定传输（上传或下载）

        Q_INVOKABLE void retryTransfer(const QString& id);  ///< 重试失败的传输

        Q_INVOKABLE void clearCompleted();                  ///< 从两个队列中移除所有已完成的传输

        Q_INVOKABLE void pauseAll();                        ///< 暂停所有运行中的传输（上传和下载）

        Q_INVOKABLE void resumeAll();                       ///< 恢复所有暂停的传输（上传和下载）

    signals:
        void activeUploadCountChanged();
        void activeDownloadCountChanged();
        void totalUploadCountChanged();
        void totalDownloadCountChanged();

    private:
        // ==================== 引擎管理 ====================

        auto DrainUploadQueue() -> void;                                                              ///< 尝试启动队列中的上传直到达到并发限制

        auto DrainDownloadQueue() -> void;                                                            ///< 尝试启动队列中的下载直到达到并发限制

        auto OnUploadFinished(const QString& transferId, bool success, const QString& error) -> void; ///< 处理上传引擎完成（成功、失败或取消）

        auto OnDownloadFinished(const QString& transferId) -> void;                                   ///< 处理下载引擎完成

        auto SaveState() -> void;                                                                     ///< 将当前队列状态持久化到磁盘

        auto LoadState() -> void;                                                                     ///< 启动时从磁盘恢复队列状态

        auto UpdateCounts() -> void;                                                                  ///< 重新计算活动计数并在更改时发射信号

        // ==================== 状态 ====================

        api::ApiClient* m_api_client;                                  ///< API 客户端
        transfers::TransferStore* m_store;                             ///< 传输存储
        utils::ConfigStore* m_config_store;                            ///< 配置存储

        transfers::TransferQueueModel* m_upload_model;                 ///< 上传模型
        transfers::TransferQueueModel* m_download_model;               ///< 下载模型

        QHash<QString, transfers::UploadEngine*> m_upload_engines;     ///< 活动上传引擎（按传输 UUID 索引）
        QHash<QString, transfers::DownloadEngine*> m_download_engines; ///< 活动下载引擎（按传输 UUID 索引）

        int m_active_upload_count{ 0 };                                ///< 活动上传数
        int m_active_download_count{ 0 };                              ///< 活动下载数
        int m_total_upload_count{ 0 };                                 ///< 总上传数
        int m_total_download_count{ 0 };                               ///< 总下载数

        inline static TransfersViewModel* s_instance = nullptr;        ///< 单例实例
        inline static QJSEngine* s_engine = nullptr;                   ///< JS 引擎实例
    };

} // namespace disk::qml::viewmodels
