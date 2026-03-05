/**
 * @file FileListModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QAbstractListModel for file/folder items in the current directory
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Pure data model — no business logic, no API calls.
 * ViewModels populate this model via ResetItems().
 *
 * Roles are aligned to the backend FileListItem DTO and the
 * file-list design spec (docs/ui/design/file-list.md).
 */

#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

#include <QtQml/qqmlregistration.h>

namespace disk::qml::models {

    /**
     * @brief Data struct for a single file/folder list entry.
     *
     * @details
     * Maps 1:1 to the backend FileListItem DTO.
     * Stored by value in a QVector inside the model.
     */
    struct FileListItemData {
        quint64 id{ 0 };
        QString name;
        QString type;     ///< "file" or "folder"
        qint64 size{ 0 }; ///< File size in bytes (0 for folders)
        QString mimeType;
        QString hash;
        int itemCount{ 0 }; ///< Child item count (folders only)
        QString createdAt;
        QString updatedAt;
    };

    /**
     * @brief QAbstractListModel exposing file/folder items to QML ListView/GridView.
     *
     * @details
     * Provides the following roles for QML delegates:
     *   - fileId, fileName, fileType, fileSize, fileMimeType,
     *     fileHash, fileItemCount, fileCreatedAt, fileUpdatedAt, fileIsFolder
     *
     * Populate via ResetItems(). The model does NOT call any APIs;
     * a ViewModel is responsible for fetching data and calling ResetItems().
     */
    class FileListModel : public QAbstractListModel {
        Q_OBJECT
        QML_ELEMENT

        /// Number of items currently in the model (convenience for QML).
        Q_PROPERTY(int count READ Count NOTIFY countChanged)

    public:
        /**
         * @brief Custom data roles exposed to QML via roleNames().
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

        // ==================== QAbstractListModel interface ====================

        [[nodiscard]] auto rowCount(const QModelIndex& parent = QModelIndex()) const -> int override;
        [[nodiscard]] auto data(const QModelIndex& index, int role = Qt::DisplayRole) const
            -> QVariant override;
        [[nodiscard]] auto roleNames() const -> QHash<int, QByteArray> override;

        // ==================== Public API ====================

        [[nodiscard]] auto Count() const -> int;

        /**
         * @brief Replace the entire model contents with @p items.
         *
         * @details
         * Emits beginResetModel / endResetModel so that bound QML views
         * refresh completely. Intended to be called by a ViewModel after
         * a successful API response.
         */
        Q_INVOKABLE void ResetItems(const QVector<FileListItemData>& items);

        /**
         * @brief Remove all items from the model.
         */
        Q_INVOKABLE void Clear();

        /**
         * @brief Retrieve the item at @p row (bounds-checked).
         *
         * @return std::nullopt when @p row is out of range.
         */
        [[nodiscard]] auto ItemAt(int row) const -> std::optional<FileListItemData>;

    signals:
        void countChanged();

    private:
        QVector<FileListItemData> m_items;
    };

} // namespace disk::qml::models
