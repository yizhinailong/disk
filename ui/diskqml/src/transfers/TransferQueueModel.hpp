/**
 * @file TransferQueueModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 传输队列模型
 * @details 用于上传/下载传输队列的 QAbstractListModel
 *
 * @copyright Copyright (c) 2026
 *
 * 角色语义：
 * - TransferIdRole : QString 传输项 UUID
 * - DirectionRole  : int 传输方向（0=上传, 1=下载）
 * - FileNameRole   : QString 文件名
 * - TotalBytesRole : qint64 总字节数
 * - DoneBytesRole  : qint64 已完成字节数
 * - StatusRole     : int 传输状态（0-4）
 * - ProgressRole   : int 进度百分比 [0, 100]
 * - SpeedRole      : qint64 传输速度（字节/秒）
 * - EtaRole        : qint64 预估剩余时间（秒）
 * - ErrorRole      : QString 错误消息
 *
 * 纯数据模型 — 无业务逻辑，无 API 调用。
 * ViewModel 或 TransferManager 通过 AddTransfer / UpdateProgress / RemoveTransfer 填充此模型。
 * 角色与 TransferItem 结构和传输面板设计规范（docs/ui/design/transfer-panel.md）对齐。
 */

#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVector>

#include <QtQml/qqmlregistration.h>

#include <transfers/TransferItem.hpp>

namespace disk::qml::transfers {

    /**
     * @brief 向 QML ListView 暴露传输项的 QAbstractListModel
     *
     * @details 为 QML 代理提供以下角色：
     *   - transferId, direction, fileName, totalBytes, doneBytes,
     *     status, progress, speed, eta, error
     *
     * 通过 AddTransfer() 填充。通过 UpdateProgress() 更新进度。
     * 模型不管理实际的文件 I/O；TransferManager 负责驱动传输并调用更新方法。
     */
    class TransferQueueModel : public QAbstractListModel {
        Q_OBJECT
        QML_ELEMENT

        /// 模型中当前的项目数量（QML 便捷属性）
        Q_PROPERTY(int count READ Count NOTIFY countChanged)

    public:
        /**
         * @brief 通过 roleNames() 向 QML 暴露的自定义数据角色
         *
         * @details 角色语义：
         *   - TransferIdRole : QString 传输项 UUID
         *   - DirectionRole  : int 传输方向（0=上传, 1=下载）
         *   - FileNameRole   : QString 文件名
         *   - TotalBytesRole : qint64 总字节数
         *   - DoneBytesRole  : qint64 已完成字节数
         *   - StatusRole     : int 传输状态（0-4）
         *   - ProgressRole   : int 进度百分比 [0, 100]
         *   - SpeedRole      : qint64 传输速度（字节/秒）
         *   - EtaRole        : qint64 预估剩余时间（秒）
         *   - ErrorRole      : QString 错误消息
         */
        enum Roles {
            TransferIdRole = Qt::UserRole + 100, ///< 传输项 UUID
            DirectionRole,                       ///< 传输方向
            FileNameRole,                        ///< 文件名
            TotalBytesRole,                      ///< 总字节数
            DoneBytesRole,                       ///< 已完成字节数
            StatusRole,                          ///< 传输状态
            ProgressRole,                        ///< 进度百分比
            SpeedRole,                           ///< 传输速度
            EtaRole,                             ///< 预估剩余时间
            ErrorRole,                           ///< 错误消息
        };
        Q_ENUM(Roles)

        explicit TransferQueueModel(QObject* parent = nullptr);
        ~TransferQueueModel() override = default;

        // ==================== QAbstractListModel 接口 ====================

        [[nodiscard]] auto rowCount(const QModelIndex& parent = QModelIndex()) const -> int override;
        [[nodiscard]] auto data(const QModelIndex& index, int role = Qt::DisplayRole) const
            -> QVariant override;
        [[nodiscard]] auto roleNames() const -> QHash<int, QByteArray> override;

        // ==================== 公共 API ====================

        [[nodiscard]] auto Count() const -> int;

        /**
         * @brief 向队列追加新传输项
         * @param item 传输数据，必须有唯一的 id
         */
        Q_INVOKABLE void AddTransfer(const TransferItem& item);

        /**
         * @brief 按 ID 移除传输项
         * @param id 传输 UUID
         */
        Q_INVOKABLE void RemoveTransfer(const QString& id);

        /**
         * @brief 更新现有传输项的进度
         */
        Q_INVOKABLE void UpdateProgress(const QString& id, qint64 doneBytes, qint64 speed, qint64 eta);

        /**
         * @brief 更新现有传输项的状态
         */
        Q_INVOKABLE void UpdateStatus(const QString& id, TransferStatus status, const QString& error = {});

        /**
         * @brief 移除所有已完成的传输项
         */
        Q_INVOKABLE void ClearCompleted();

        /**
         * @brief 移除所有失败的传输项
         */
        Q_INVOKABLE void ClearFailed();

        /**
         * @brief 将所有正在传输的项设为暂停
         */
        Q_INVOKABLE void PauseAll();

        /**
         * @brief 将所有暂停的项设为排队（准备好被引擎处理）
         */
        Q_INVOKABLE void ResumeAll();

        /**
         * @brief 从模型中移除所有项
         */
        Q_INVOKABLE void Clear();

        /**
         * @brief 检索 @p row 处的项目（边界检查）
         * @return 当 @p row 超出范围时返回 std::nullopt
         */
        [[nodiscard]] auto ItemAt(int row) const -> std::optional<TransferItem>;

        /**
         * @brief 获取所有项（用于持久化）
         */
        [[nodiscard]] auto Items() const -> const QVector<TransferItem>&;

        /**
         * @brief 批量替换整个模型内容（用于从磁盘恢复）
         */
        void ResetItems(const QVector<TransferItem>& items);

    signals:
        void countChanged();

    private:
        /**
         * @brief 查找给定传输 ID 的行索引
         * @return 未找到时返回 -1
         */
        [[nodiscard]] auto FindRow(const QString& id) const -> int;

        /**
         * @brief 对单行的所有角色发出 dataChanged 信号
         */
        void EmitRowChanged(int row);

        /**
         * @brief 移除匹配给定状态的所有项
         */
        void RemoveByStatus(TransferStatus status);

        QVector<TransferItem> m_items;
        QHash<QString, int> m_id_index; ///< id → 行索引，用于 O(1) 查找
    };

} // namespace disk::qml::transfers
