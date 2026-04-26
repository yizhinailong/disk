/**
 * @file FolderTreeModel.cpp
 * @brief FolderTreeModel implementation
 *
 * @copyright Copyright (c) 2026
 */

#include "models/FolderTreeModel.hpp"

#include <QJsonArray>
#include <QJsonObject>

namespace disk::desktop {

    auto FolderNode::FromJson(const QJsonObject& json) -> FolderNode {
        FolderNode node;
        node.id = static_cast<quint64>(json.value("id").toDouble(0));
        node.name = json.value("name").toString();

        if (json.contains("children") && json["children"].isArray()) {
            const auto arr = json["children"].toArray();
            node.children.reserve(arr.size());
            for (const auto& child : arr) {
                node.children.append(FromJson(child.toObject()));
            }
        }

        return node;
    }

    auto FolderNode::ToJson() const -> QJsonObject {
        QJsonObject json;
        json["id"] = static_cast<double>(id);
        json["name"] = name;

        if (!children.isEmpty()) {
            QJsonArray arr;
            for (const auto& child : children) {
                arr.append(child.ToJson());
            }
            json["children"] = arr;
        }

        return json;
    }

    FolderTreeModel::FolderTreeModel(QObject* parent)
        : QAbstractItemModel(parent) {}

    FolderTreeModel::~FolderTreeModel() = default;

    auto FolderTreeModel::index(int row, int column, const QModelIndex& parent) const -> QModelIndex {
        if (row < 0 || column < 0 || column >= 1) {
            return {};
        }

        const auto* parent_node = parent.isValid() ? NodeFromIndex(parent) : &m_root;
        if (!parent_node || row >= parent_node->children.size()) {
            return {};
        }

        return createIndex(row, column, const_cast<FolderNode*>(&parent_node->children.at(row)));
    }

    auto FolderTreeModel::parent(const QModelIndex& child) const -> QModelIndex {
        if (!child.isValid()) {
            return {};
        }

        auto* child_node = NodeFromIndex(child);
        if (!child_node) {
            return {};
        }

        auto [parent_node, _] = FindParentOf(child_node, m_root);
        if (!parent_node || parent_node == &m_root) {
            return {};
        }

        auto [grandparent, row_in_grandparent] = FindParentOf(parent_node, m_root);
        if (!grandparent || row_in_grandparent < 0) {
            return {};
        }

        return createIndex(row_in_grandparent, 0, const_cast<FolderNode*>(parent_node));
    }

    auto FolderTreeModel::rowCount(const QModelIndex& parent) const -> int {
        if (parent.column() > 0) {
            return 0;
        }
        const auto* node = parent.isValid() ? NodeFromIndex(parent) : &m_root;
        return node ? node->children.size() : 0;
    }

    auto FolderTreeModel::columnCount(const QModelIndex& /*parent*/) const -> int {
        return 1;
    }

    auto FolderTreeModel::data(const QModelIndex& index, int role) const -> QVariant {
        if (!index.isValid()) {
            return {};
        }

        const auto* node = NodeFromIndex(index);
        if (!node) {
            return {};
        }

        switch (role) {
            case IdRole  : return node->id;
            case NameRole: return node->name;
            default      : return {};
        }
    }

    auto FolderTreeModel::roleNames() const -> QHash<int, QByteArray> {
        return {
            {   IdRole,   "folderId" },
            { NameRole, "folderName" },
        };
    }

    auto FolderTreeModel::SetRoot(const FolderNode& root) -> void {
        beginResetModel();
        m_root = root;
        endResetModel();
    }

    auto FolderTreeModel::GetNode(const QModelIndex& index) const -> const FolderNode* {
        if (!index.isValid()) {
            return &m_root;
        }
        return NodeFromIndex(index);
    }

    auto FolderTreeModel::indexOf(quint64 id) const -> QModelIndex {
        return IndexOfNode(&m_root, id);
    }

    auto FolderTreeModel::NodeFromIndex(const QModelIndex& index) const -> const FolderNode* {
        return static_cast<const FolderNode*>(index.internalPointer());
    }

    auto FolderTreeModel::FindParentOf(const FolderNode* target, const FolderNode& root) const -> std::pair<const FolderNode*, int> {
        for (int i = 0; i < root.children.size(); ++i) {
            if (&root.children[i] == target) {
                return { &root, i };
            }
            auto result = FindParentOf(target, root.children[i]);
            if (result.first) {
                return result;
            }
        }
        return { nullptr, -1 };
    }

    auto FolderTreeModel::IndexOfNode(const FolderNode* node, quint64 id) const -> QModelIndex {
        for (int i = 0; i < node->children.size(); ++i) {
            if (node->children[i].id == id) {
                return createIndex(i, 0, const_cast<FolderNode*>(&node->children[i]));
            }
            auto result = IndexOfNode(&node->children[i], id);
            if (result.isValid()) {
                return result;
            }
        }
        return {};
    }

} // namespace disk::desktop
