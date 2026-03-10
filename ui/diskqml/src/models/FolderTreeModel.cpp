/**
 * @file FolderTreeModel.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QAbstractItemModel implementation for folder tree hierarchy
 *
 * @copyright Copyright (c) 2026
 */

#include "models/FolderTreeModel.hpp"

namespace disk::qml::models {

    FolderTreeModel::FolderTreeModel(QObject* parent)
        : QAbstractItemModel(parent) {}

    // ==================== QAbstractItemModel interface ====================

    auto FolderTreeModel::index(int row, int column, const QModelIndex& parent) const -> QModelIndex {
        if (column != 0) {
            return {};
        }

        if (!parent.isValid()) {
            // Root-level: row indexes into m_root_indices
            if (row < 0 || row >= m_root_indices.size()) {
                return {};
            }
            const int nodeIdx = m_root_indices.at(row);
            return createIndex(row, 0, quintptr(nodeIdx));
        }

        // Child of an existing node
        const auto parentNodeIdx = static_cast<int>(parent.internalId());
        if (parentNodeIdx < 0 || parentNodeIdx >= m_nodes.size()) {
            return {};
        }

        const auto& parentNode = m_nodes.at(parentNodeIdx);
        if (row < 0 || row >= parentNode.childIndices.size()) {
            return {};
        }

        const int childNodeIdx = parentNode.childIndices.at(row);
        return createIndex(row, 0, quintptr(childNodeIdx));
    }

    auto FolderTreeModel::parent(const QModelIndex& child) const -> QModelIndex {
        if (!child.isValid()) {
            return {};
        }

        const auto nodeIdx = static_cast<int>(child.internalId());
        if (nodeIdx < 0 || nodeIdx >= m_nodes.size()) {
            return {};
        }

        const auto& node = m_nodes.at(nodeIdx);
        if (node.parentIndex < 0) {
            // Root-level node — no parent
            return {};
        }

        const auto& parentNode = m_nodes.at(node.parentIndex);

        // Find which row the parent is in its own parent's child list
        if (parentNode.parentIndex < 0) {
            // Parent is root-level
            const int row = m_root_indices.indexOf(node.parentIndex);
            return createIndex(row, 0, quintptr(node.parentIndex));
        }

        const auto& grandparent = m_nodes.at(parentNode.parentIndex);
        const int row = grandparent.childIndices.indexOf(node.parentIndex);
        return createIndex(row, 0, quintptr(node.parentIndex));
    }

    auto FolderTreeModel::rowCount(const QModelIndex& parent) const -> int {
        if (!parent.isValid()) {
            return static_cast<int>(m_root_indices.size());
        }

        const auto nodeIdx = static_cast<int>(parent.internalId());
        if (nodeIdx < 0 || nodeIdx >= m_nodes.size()) {
            return 0;
        }

        return static_cast<int>(m_nodes.at(nodeIdx).childIndices.size());
    }

    auto FolderTreeModel::columnCount(const QModelIndex& /*parent*/) const -> int {
        return 1; // single-column tree
    }

    auto FolderTreeModel::data(const QModelIndex& index, int role) const -> QVariant {
        if (!index.isValid()) {
            return {};
        }

        const auto nodeIdx = static_cast<int>(index.internalId());
        if (nodeIdx < 0 || nodeIdx >= m_nodes.size()) {
            return {};
        }

        const auto& node = m_nodes.at(nodeIdx);

        switch (role) {
            case FolderIdRole   : return QVariant::fromValue(node.id);
            case FolderNameRole : return node.name;
            case Qt::DisplayRole: return node.name;
            default             : return {};
        }
    }

    auto FolderTreeModel::roleNames() const -> QHash<int, QByteArray> {
        static const QHash<int, QByteArray> roles{
            {   FolderIdRole,   "folderId" },
            { FolderNameRole, "folderName" },
        };
        return roles;
    }

    // ==================== Public API ====================

    auto FolderTreeModel::Loading() const -> bool {
        return m_loading;
    }

    auto FolderTreeModel::SetLoading(bool loading) -> void {
        if (m_loading == loading) {
            return;
        }
        m_loading = loading;
        emit loadingChanged();
    }

    void FolderTreeModel::PopulateFromDto(const QVector<FolderTreeNodeDto>& roots) {
        beginResetModel();
        m_nodes.clear();
        m_root_indices.clear();

        for (const auto& root : roots) {
            FlattenNode(root, -1);
        }

        endResetModel();
    }

    void FolderTreeModel::Clear() {
        if (m_nodes.isEmpty()) {
            return;
        }
        beginResetModel();
        m_nodes.clear();
        m_root_indices.clear();
        endResetModel();
    }

    // ==================== Private ====================

    void FolderTreeModel::FlattenNode(const FolderTreeNodeDto& dto, int parentIndex) {
        const int myIndex = static_cast<int>(m_nodes.size());

        TreeNode node;
        node.id = dto.id;
        node.name = dto.name;
        node.parentIndex = parentIndex;
        // childIndices populated below after children are added
        m_nodes.append(node);

        if (parentIndex < 0) {
            m_root_indices.append(myIndex);
        } else {
            m_nodes[parentIndex].childIndices.append(myIndex);
        }

        // Recursively add children
        for (const auto& child : dto.children) {
            FlattenNode(child, myIndex);
        }
    }

} // namespace disk::qml::models
