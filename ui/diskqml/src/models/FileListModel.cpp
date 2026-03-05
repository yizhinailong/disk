/**
 * @file FileListModel.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QAbstractListModel implementation for file/folder items
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 */

#include "models/FileListModel.hpp"

namespace disk::qml::models {

    FileListModel::FileListModel(QObject* parent)
        : QAbstractListModel(parent) {}

    // ==================== QAbstractListModel interface ====================

    auto FileListModel::rowCount(const QModelIndex& parent) const -> int {
        if (parent.isValid()) {
            return 0; // flat list — no children
        }
        return static_cast<int>(m_items.size());
    }

    auto FileListModel::data(const QModelIndex& index, int role) const -> QVariant {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
            return {};
        }

        const auto& item = m_items.at(index.row());

        switch (role) {
            case FileIdRole       : return QVariant::fromValue(item.id);
            case FileNameRole     : return item.name;
            case FileTypeRole     : return item.type;
            case FileSizeRole     : return QVariant::fromValue(item.size);
            case FileMimeTypeRole : return item.mimeType;
            case FileHashRole     : return item.hash;
            case FileItemCountRole: return item.itemCount;
            case FileCreatedAtRole: return item.createdAt;
            case FileUpdatedAtRole: return item.updatedAt;
            case FileIsFolderRole : return (item.type == QStringLiteral("folder"));
            default               : return {};
        }
    }

    auto FileListModel::roleNames() const -> QHash<int, QByteArray> {
        static const QHash<int, QByteArray> roles{
            {        FileIdRole,        "fileId" },
            {      FileNameRole,      "fileName" },
            {      FileTypeRole,      "fileType" },
            {      FileSizeRole,      "fileSize" },
            {  FileMimeTypeRole,  "fileMimeType" },
            {      FileHashRole,      "fileHash" },
            { FileItemCountRole, "fileItemCount" },
            { FileCreatedAtRole, "fileCreatedAt" },
            { FileUpdatedAtRole, "fileUpdatedAt" },
            {  FileIsFolderRole,  "fileIsFolder" },
        };
        return roles;
    }

    // ==================== Public API ====================

    auto FileListModel::Count() const -> int {
        return static_cast<int>(m_items.size());
    }

    void FileListModel::ResetItems(const QVector<FileListItemData>& items) {
        beginResetModel();
        m_items = items;
        endResetModel();
        emit countChanged();
    }

    void FileListModel::Clear() {
        if (m_items.isEmpty()) {
            return;
        }
        beginResetModel();
        m_items.clear();
        endResetModel();
        emit countChanged();
    }

    auto FileListModel::ItemAt(int row) const -> std::optional<FileListItemData> {
        if (row < 0 || row >= m_items.size()) {
            return std::nullopt;
        }
        return m_items.at(row);
    }

} // namespace disk::qml::models
