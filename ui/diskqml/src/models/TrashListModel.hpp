/**
 * @file TrashListModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 回收站项的 QAbstractListModel
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 纯数据模型 —— 无业务逻辑，无 API 调用。
 * ViewModel 通过 ResetItems() 填充此模型。
 *
 * 角色与后端 TrashItemResponse DTO 和回收站设计规范 (docs/ui/design/trash.md) 对齐。
 */

#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

#include <QtQml/qqmlregistration.h>

namespace disk::qml::models {

    /**
     * @brief 单个回收站列表项的数据结构。
     *
     * @details
     * 与后端 TrashItemResponse DTO 一一映射。
     * 在模型内部的 QVector 中按值存储。
     */
    struct TrashListItemData {
        quint64 id{ 0 };
        QString type; ///< "file"（文件）或 "folder"（文件夹）
        quint64 originalId{ 0 };
        QString name;
        qint64 size{ 0 }; ///< 文件大小（字节），文件夹为 0；qint64 用于 QML 兼容
        QString originalPath;
        QString deletedAt;
        QString expiresAt;
    };

    /**
     * @brief 向 QML ListView 暴露回收站项的 QAbstractListModel。
     *
     * @details
     * 为 QML 代理提供以下角色：
     *   - trashId（回收站ID）, trashType（类型）, trashOriginalId（原ID）,
     *     trashName（名称）, trashSize（大小）, trashOriginalPath（原路径）,
     *     trashDeletedAt（删除时间）, trashExpiresAt（过期时间）, trashIsFolder（是否文件夹）
     *
     * 通过 ResetItems() 填充。模型不调用任何 API；
     * ViewModel 负责获取数据并调用 ResetItems()。
     */
    class TrashListModel : public QAbstractListModel {
        Q_OBJECT
        QML_ELEMENT

        /// 当前模型中的项数量（QML 便捷属性）。
        Q_PROPERTY(int count READ Count NOTIFY countChanged)

    public:
        /**
         * @brief 通过 roleNames() 向 QML 暴露的自定义数据角色。
         */
        enum Roles {
            TrashIdRole = Qt::UserRole + 1,
            TrashTypeRole,
            TrashOriginalIdRole,
            TrashNameRole,
            TrashSizeRole,
            TrashOriginalPathRole,
            TrashDeletedAtRole,
            TrashExpiresAtRole,
            TrashIsFolderRole,
        };
        Q_ENUM(Roles)

        explicit TrashListModel(QObject* parent = nullptr);
        ~TrashListModel() override = default;

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
        Q_INVOKABLE void ResetItems(const QVector<TrashListItemData>& items);

        /**
         * @brief 从模型中移除所有项。
         */
        Q_INVOKABLE void Clear();

        /**
         * @brief 获取 @p row 处的项（带边界检查）。
         *
         * @return 当 @p row 超出范围时返回 std::nullopt。
         */
        [[nodiscard]] auto ItemAt(int row) const -> std::optional<TrashListItemData>;

    signals:
        void countChanged();

    private:
        QVector<TrashListItemData> m_items;
    };

} // namespace disk::qml::models
