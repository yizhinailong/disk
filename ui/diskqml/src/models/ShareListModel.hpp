/**
 * @file ShareListModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 分享项的 QAbstractListModel
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 纯数据模型 —— 无业务逻辑，无 API 调用。
 * ViewModel 通过 ResetItems() 填充此模型。
 *
 * 角色与后端 ShareListItemDto 和分享设计规范 (docs/ui/design/share.md) 对齐。
 */

#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

#include <QtQml/qqmlregistration.h>

namespace disk::qml::models {

    /**
     * @brief 单个分享列表项的数据结构。
     *
     * @details
     * 与 ShareListItemDto 一一映射。
     * 在模型内部的 QVector 中按值存储。
     */
    struct ShareListItemData {
        QString shareId;
        QString fileName;
        int fileCount{ 0 };
        QString shareLink;
        bool hasPassword{ false };
        QString permission;
        int viewCount{ 0 };
        int downloadCount{ 0 };
        QString createdAt;
        QString expiresAt;
        QString status;
    };

    /**
     * @brief 向 QML ListView 暴露分享项的 QAbstractListModel。
     *
     * @details
     * 为 QML 代理提供以下角色：
     *   - shareId（分享ID）, shareFileName（文件名）, shareFileCount（文件数）,
     *     shareLink（分享链接）, shareHasPassword（是否有密码）, sharePermission（权限）,
     *     shareViewCount（浏览次数）, shareDownloadCount（下载次数）,
     *     shareCreatedAt（创建时间）, shareExpiresAt（过期时间）, shareStatus（状态）
     *
     * 通过 ResetItems() 填充。模型不调用任何 API；
     * ViewModel 负责获取数据并调用 ResetItems()。
     */
    class ShareListModel : public QAbstractListModel {
        Q_OBJECT
        QML_ELEMENT

        /// 当前模型中的项数量（QML 便捷属性）。
        Q_PROPERTY(int count READ Count NOTIFY countChanged)

    public:
        /**
         * @brief 通过 roleNames() 向 QML 暴露的自定义数据角色。
         */
        enum Roles {
            ShareIdRole = Qt::UserRole + 1,
            ShareFileNameRole,
            ShareFileCountRole,
            ShareLinkRole,
            ShareHasPasswordRole,
            SharePermissionRole,
            ShareViewCountRole,
            ShareDownloadCountRole,
            ShareCreatedAtRole,
            ShareExpiresAtRole,
            ShareStatusRole,
        };
        Q_ENUM(Roles)

        explicit ShareListModel(QObject* parent = nullptr);
        ~ShareListModel() override = default;

        // ==================== QAbstractListModel 接口 ====================

        [[nodiscard]] auto rowCount(const QModelIndex& parent = QModelIndex()) const -> int override;
        [[nodiscard]] auto data(const QModelIndex& index, int role = Qt::DisplayRole) const
            -> QVariant override;
        [[nodiscard]] auto roleNames() const -> QHash<int, QByteArray> override;

        // ==================== 公共 API ====================

        [[nodiscard]] auto Count() const -> int;

        /**
         * @brief 用 @p items 替换整个模型内容。
         *
         * @details
         * 发射 beginResetModel / endResetModel 信号，使绑定的 QML 视图完全刷新。
         * 旨在由 ViewModel 在 API 响应成功后调用。
         */
        Q_INVOKABLE void ResetItems(const QVector<ShareListItemData>& items);

        /**
         * @brief 从模型中移除所有项。
         */
        Q_INVOKABLE void Clear();

        /**
         * @brief 获取 @p row 处的项（带边界检查）。
         *
         * @return 当 @p row 超出范围时返回 std::nullopt。
         */
        [[nodiscard]] auto ItemAt(int row) const -> std::optional<ShareListItemData>;

    signals:
        void countChanged();

    private:
        QVector<ShareListItemData> m_items;
    };

} // namespace disk::qml::models
