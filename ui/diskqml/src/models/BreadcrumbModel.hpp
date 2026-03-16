/**
 * @file BreadcrumbModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 面包屑导航路径的 QAbstractListModel
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 纯数据模型 —— 无业务逻辑，无 API 调用。
 * ViewModel 通过 ResetPath() 填充此模型。
 *
 * 角色与后端 BreadcrumbItem DTO (src/dtos/FolderDto.hpp) 对齐。
 */

#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

#include <QtQml/qqmlregistration.h>

namespace disk::qml::models {

    /**
     * @brief 单个面包屑路径段的数据结构。
     *
     * @details
     * 与后端 BreadcrumbItem DTO 一一映射。
     * id == 0 表示根目录。
     */
    struct BreadcrumbItemData {
        quint64 id{ 0 };
        QString name;
    };

    /**
     * @brief 向 QML 暴露面包屑路径项的 QAbstractListModel。
     *
     * @details
     * 为 QML 代理提供以下角色：
     *   - folderId（文件夹ID）, folderName（文件夹名称）
     *
     * 通过 ResetPath() 填充。模型不调用任何 API；
     * ViewModel 负责获取数据并调用 ResetPath()。
     *
     * 路径始终从根文件夹开始，以当前查看的文件夹结束。
     */
    class BreadcrumbModel : public QAbstractListModel {
        Q_OBJECT
        QML_ELEMENT

        /// 当前模型中的路径段数量。
        Q_PROPERTY(int count READ Count NOTIFY countChanged)

    public:
        /**
         * @brief 通过 roleNames() 向 QML 暴露的自定义数据角色。
         */
        enum Roles {
            FolderIdRole = Qt::UserRole + 1,
            FolderNameRole,
        };
        Q_ENUM(Roles)

        explicit BreadcrumbModel(QObject* parent = nullptr);
        ~BreadcrumbModel() override = default;

        // ==================== QAbstractListModel 接口 ====================

        [[nodiscard]] auto rowCount(const QModelIndex& parent = QModelIndex()) const -> int override;
        [[nodiscard]] auto data(const QModelIndex& index, int role = Qt::DisplayRole) const
            -> QVariant override;
        [[nodiscard]] auto roleNames() const -> QHash<int, QByteArray> override;

        // ==================== 公共 API ====================

        [[nodiscard]] auto Count() const -> int;

        /**
         * @brief 用 @p path 替换整个面包屑路径。
         *
         * @details
         * 发射 beginResetModel / endResetModel 信号，使绑定的 QML 视图完全刷新。
         */
        Q_INVOKABLE void ResetPath(const QVector<BreadcrumbItemData>& path);

        /**
         * @brief 从模型中移除所有路径段。
         */
        Q_INVOKABLE void Clear();

        /**
         * @brief 获取最后一个（当前）面包屑项的文件夹 ID。
         *
         * @return 如果模型为空（根目录），返回 0。
         */
        [[nodiscard]] auto CurrentFolderId() const -> quint64;

        /**
         * @brief 获取 @p row 处的项（带边界检查）。
         *
         * @return 当 @p row 超出范围时返回 std::nullopt。
         */
        [[nodiscard]] auto ItemAt(int row) const -> std::optional<BreadcrumbItemData>;

    signals:
        void countChanged();

    private:
        QVector<BreadcrumbItemData> m_path;
    };

} // namespace disk::qml::models
