/**
 * @file FolderTreeModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QAbstractItemModel for folder tree hierarchy (used by FolderPickerDialog)
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Tree model backed by a flat vector of TreeNode structs with parent pointers.
 * Populated from FolderTreeNodeDto (recursive) via PopulateFromDto().
 * Exposes "folderId" and "folderName" roles for QML TreeView delegates.
 *
 * Pure data model — no business logic, no API calls.
 * A ViewModel is responsible for calling FolderService::GetFolderTree()
 * and then calling PopulateFromDto() on this model.
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
     * @brief Internal tree node stored by value in a flat vector.
     *
     * @details
     * Each node holds its parent index and child indices into the same vector.
     * Index -1 means "no parent" (root-level node).
     */
    struct TreeNode {
        quint64 id{ 0 };
        QString name;
        int parentIndex{ -1 };     ///< index in m_nodes (-1 = root-level)
        QVector<int> childIndices; ///< indices of children in m_nodes
    };

    /**
     * @brief QAbstractItemModel exposing a folder tree to QML TreeView.
     *
     * @details
     * Provides the following roles for QML delegates:
     *   - folderId   : quint64 folder ID
     *   - folderName : QString folder display name
     *
     * Populate via PopulateFromDto(). The model does NOT call any APIs;
     * a ViewModel is responsible for fetching data and calling PopulateFromDto().
     */
    class FolderTreeModel : public QAbstractItemModel {
        Q_OBJECT
        QML_ELEMENT

        /// True while the tree data is being loaded.
        Q_PROPERTY(bool loading READ Loading WRITE SetLoading NOTIFY loadingChanged)

    public:
        enum Roles {
            FolderIdRole = Qt::UserRole + 1,
            FolderNameRole,
        };
        Q_ENUM(Roles)

        explicit FolderTreeModel(QObject* parent = nullptr);
        ~FolderTreeModel() override = default;

        // ==================== QAbstractItemModel interface ====================

        [[nodiscard]] auto index(int row, int column, const QModelIndex& parent = QModelIndex()) const
            -> QModelIndex override;
        [[nodiscard]] auto parent(const QModelIndex& child) const -> QModelIndex override;
        [[nodiscard]] auto rowCount(const QModelIndex& parent = QModelIndex()) const -> int override;
        [[nodiscard]] auto columnCount(const QModelIndex& parent = QModelIndex()) const -> int override;
        [[nodiscard]] auto data(const QModelIndex& index, int role = Qt::DisplayRole) const
            -> QVariant override;
        [[nodiscard]] auto roleNames() const -> QHash<int, QByteArray> override;

        // ==================== Public API ====================

        [[nodiscard]] auto Loading() const -> bool;
        auto SetLoading(bool loading) -> void;

        /**
         * @brief Replace the entire tree with data from DTO nodes.
         *
         * @details
         * Recursively flattens the FolderTreeNodeDto hierarchy into the
         * internal m_nodes vector, then emits modelReset.
         */
        void PopulateFromDto(const QVector<FolderTreeNodeDto>& roots);

        /**
         * @brief Remove all nodes from the model.
         */
        void Clear();

    signals:
        void loadingChanged();

    private:
        /// Recursively flatten a DTO node into m_nodes.
        void FlattenNode(const FolderTreeNodeDto& dto, int parentIndex);

        QVector<TreeNode> m_nodes;
        QVector<int> m_root_indices; ///< indices of top-level nodes
        bool m_loading{ false };
    };

} // namespace disk::qml::models
