/**
 * @file FileListModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 当前目录中文件/文件夹项的 QAbstractListModel
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 纯数据模型 —— 无业务逻辑，无 API 调用。
 * ViewModel 通过 ResetItems() 填充此模型。
 *
 * 角色与后端 FileListItem DTO 和文件列表设计规范 (docs/ui/design/file-list.md) 对齐。
 */

#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

#include <QtQml/qqmlregistration.h>

namespace disk::qml::models {

    /**
     * @brief 单个文件/文件夹列表项的数据结构。
     *
     * @details
     * 与后端 FileListItem DTO 一一映射。
     * 在模型内部的 QVector 中按值存储。
     */
    struct FileListItemData {
        quint64 id{ 0 };
        QString name;
        QString type;     ///< "file"（文件）或 "folder"（文件夹）
        qint64 size{ 0 }; ///< 文件大小（字节），文件夹为 0
        QString mimeType;
        QString hash;
        int itemCount{ 0 }; ///< 子项数量（仅文件夹）
        QString createdAt;
        QString updatedAt;
    };

    /**
     * @brief 向 QML ListView/GridView 暴露文件/文件夹项的 QAbstractListModel。
     *
     * @details
     * 为 QML 代理提供以下角色：
     *   - fileId（文件ID）, fileName（文件名）, fileType（文件类型）, fileSize（文件大小）,
     *     fileMimeType（MIME类型）, fileHash（文件哈希）, fileItemCount（子项数量）,
     *     fileCreatedAt（创建时间）, fileUpdatedAt（更新时间）, fileIsFolder（是否文件夹）
     *
     * 通过 ResetItems() 填充。模型不调用任何 API；
     * ViewModel 负责获取数据并调用 ResetItems()。
     */
    class FileListModel : public QAbstractListModel {
        Q_OBJECT
        QML_ELEMENT

        /// 当前模型中的项数量（QML 便捷属性）。
        Q_PROPERTY(int count READ Count NOTIFY countChanged)

    public:
        /**
         * @brief 通过 roleNames() 向 QML 暴露的自定义数据角色。
         */
        enum Roles {
            FileIdRole = Qt::UserRole + 1,
            FileNameRole,
            FileTypeRole,
            FileSizeRole,
            FileMimeTypeRole,
            FileHashRole,
            FileItemCountRole,
            FileCreatedAtRole,
            FileUpdatedAtRole,
            FileIsFolderRole,
        };
        Q_ENUM(Roles)

        explicit FileListModel(QObject* parent = nullptr);
        ~FileListModel() override = default;

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
        Q_INVOKABLE void ResetItems(const QVector<FileListItemData>& items);

        /**
         * @brief 从模型中移除所有项。
         */
        Q_INVOKABLE void Clear();

        /**
         * @brief 获取 @p row 处的项（带边界检查）。
         *
         * @return 当 @p row 超出范围时返回 std::nullopt。
         */
        [[nodiscard]] auto ItemAt(int row) const -> std::optional<FileListItemData>;

    signals:
        void countChanged();

    private:
        QVector<FileListItemData> m_items;
    };

} // namespace disk::qml::models
