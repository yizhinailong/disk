/**
 * @file FolderTreeModel.hpp
 * @brief QAbstractItemModel for directory tree per doc 02 §3.6
 *
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <QAbstractItemModel>
#include <QJsonObject>
#include <QVector>

namespace disk::desktop {

    struct FolderNode {
        quint64 id{ 0 };
        QString name;
        QVector<FolderNode> children;

        static auto FromJson(const QJsonObject& json) -> FolderNode;
        auto ToJson() const -> QJsonObject;
    };

    class FolderTreeModel : public QAbstractItemModel {
        Q_OBJECT

    public:
        enum Roles {
            IdRole = Qt::UserRole + 1,
            NameRole,
        };

        explicit FolderTreeModel(QObject* parent = nullptr);
        ~FolderTreeModel() override;

        auto index(int row, int column, const QModelIndex& parent = {}) const -> QModelIndex override;
        auto parent(const QModelIndex& child) const -> QModelIndex override;
        auto rowCount(const QModelIndex& parent = {}) const -> int override;
        auto columnCount(const QModelIndex& parent = {}) const -> int override;
        auto data(const QModelIndex& index, int role) const -> QVariant override;
        auto roleNames() const -> QHash<int, QByteArray> override;

        auto SetRoot(const FolderNode& root) -> void;
        auto GetNode(const QModelIndex& index) const -> const FolderNode*;

        Q_INVOKABLE QModelIndex indexOf(quint64 id) const;

    private:
        auto NodeFromIndex(const QModelIndex& index) const -> const FolderNode*;
        auto IndexOfNode(const FolderNode* node, quint64 id) const -> QModelIndex;
        auto FindParentOf(const FolderNode* target, const FolderNode& root) const -> std::pair<const FolderNode*, int>;

        FolderNode m_root;
    };

} // namespace disk::desktop
