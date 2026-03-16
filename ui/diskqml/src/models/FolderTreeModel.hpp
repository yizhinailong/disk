/**
 * @file FolderTreeModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 文件夹树层次结构的 QAbstractItemModel（用于 FolderPickerDialog）
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * 由带有父指针的扁平 TreeNode 结构向量支持的树模型。
 * 通过 FolderTreeNodeDto（递归）经由 PopulateFromDto() 填充。
 * 为 QML TreeView 代理暴露 "folderId" 和 "folderName" 角色。
 *
 * 纯数据模型 —— 无业务逻辑，无 API 调用。
 * ViewModel 负责调用 FolderService::GetFolderTree()
 * 然后调用此模型的 PopulateFromDto()。
 */

#pragma once

#include <QAbstractItemModel>
#include <QString>
#include <QVector>
#include <memory>

#include <QtQml/qqmlregistration.h>

#include <dtos/FileDtos.hpp>

namespace disk::qml::models {

    /**
     * @brief 在扁平向量中按值存储的内部树节点。
     *
     * @details
     * 每个节点持有其父索引和子索引到同一个向量中。
     * 索引 -1 表示"无父节点"（根级节点）。
     */
    struct TreeNode {
        quint64 id{ 0 };
        QString name;
        int parentIndex{ -1 };     ///< m_nodes 中的索引（-1 = 根级）
        QVector<int> childIndices; ///< m_nodes 中子项的索引
    };

    /**
     * @brief 向 QML TreeView 暴露文件夹树的 QAbstractItemModel。
     *
     * @details
     * 为 QML 代理提供以下角色：
     *   - folderId（文件夹ID）   : quint64 文件夹 ID
     *   - folderName（文件夹名称） : QString 文件夹显示名称
     *
     * 通过 PopulateFromDto() 填充。模型不调用任何 API；
     * ViewModel 负责获取数据并调用 PopulateFromDto()。
     */
    class FolderTreeModel : public QAbstractItemModel {
        Q_OBJECT
        QML_ELEMENT

        /// 树数据正在加载时为 true。
        Q_PROPERTY(bool loading READ Loading WRITE SetLoading NOTIFY loadingChanged)

    public:
        enum Roles {
            FolderIdRole = Qt::UserRole + 1,
            FolderNameRole,
        };
        Q_ENUM(Roles)

        explicit FolderTreeModel(QObject* parent = nullptr);
        ~FolderTreeModel() override = default;

        // ==================== QAbstractItemModel 接口 ====================

        [[nodiscard]] auto index(int row, int column, const QModelIndex& parent = QModelIndex()) const
            -> QModelIndex override;
        [[nodiscard]] auto parent(const QModelIndex& child) const -> QModelIndex override;
        [[nodiscard]] auto rowCount(const QModelIndex& parent = QModelIndex()) const -> int override;
        [[nodiscard]] auto columnCount(const QModelIndex& parent = QModelIndex()) const -> int override;
        [[nodiscard]] auto data(const QModelIndex& index, int role = Qt::DisplayRole) const
            -> QVariant override;
        [[nodiscard]] auto roleNames() const -> QHash<int, QByteArray> override;

        // ==================== 公共 API ====================

        [[nodiscard]] auto Loading() const -> bool;
        auto SetLoading(bool loading) -> void;

        /**
         * @brief 用 DTO 节点数据替换整个树。
         *
         * @details
         * 递归地将 FolderTreeNodeDto 层次结构扁平化到
         * 内部 m_nodes 向量中，然后发射 modelReset 信号。
         */
        void PopulateFromDto(const QVector<FolderTreeNodeDto>& roots);

        /**
         * @brief 从模型中移除所有节点。
         */
        void Clear();

    signals:
        void loadingChanged();

    private:
        /// 递归地将 DTO 节点扁平化到 m_nodes 中。
        void FlattenNode(const FolderTreeNodeDto& dto, int parentIndex);

        QVector<TreeNode> m_nodes;
        QVector<int> m_root_indices; ///< 顶层节点的索引
        bool m_loading{ false };
    };

} // namespace disk::qml::models
