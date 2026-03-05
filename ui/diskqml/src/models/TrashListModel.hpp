/**
 * @file TrashListModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QAbstractListModel for trash items
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Pure data model — no business logic, no API calls.
 * ViewModels populate this model via ResetItems().
 *
 * Roles are aligned to the backend TrashItemResponse DTO and the
 * trash design spec (docs/ui/design/trash.md).
 */

#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

#include <QtQml/qqmlregistration.h>

namespace disk::qml::models {

    /**
     * @brief Data struct for a single trash list entry.
     *
     * @details
     * Maps 1:1 to the backend TrashItemResponse DTO.
     * Stored by value in a QVector inside the model.
     */
    struct TrashListItemData {
        quint64 id{ 0 };
        QString type; ///< "file" or "folder"
        quint64 originalId{ 0 };
        QString name;
        qint64 size{ 0 }; ///< File size in bytes (0 for folders); qint64 for QML compat
        QString originalPath;
        QString deletedAt;
        QString expiresAt;
    };

    /**
     * @brief QAbstractListModel exposing trash items to QML ListView.
     *
     * @details
     * Provides the following roles for QML delegates:
     *   - trashId, trashType, trashOriginalId, trashName, trashSize,
     *     trashOriginalPath, trashDeletedAt, trashExpiresAt, trashIsFolder
     *
     * Populate via ResetItems(). The model does NOT call any APIs;
     * a ViewModel is responsible for fetching data and calling ResetItems().
     */
    class TrashListModel : public QAbstractListModel {
        Q_OBJECT
        QML_ELEMENT

        /// Number of items currently in the model (convenience for QML).
        Q_PROPERTY(int count READ Count NOTIFY countChanged)

    public:
        /**
         * @brief Custom data roles exposed to QML via roleNames().
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
        Q_INVOKABLE void ResetItems(const QVector<TrashListItemData>& items);

        /**
         * @brief Remove all items from the model.
         */
        Q_INVOKABLE void Clear();

        /**
         * @brief Retrieve the item at @p row (bounds-checked).
         *
         * @return std::nullopt when @p row is out of range.
         */
        [[nodiscard]] auto ItemAt(int row) const -> std::optional<TrashListItemData>;

    signals:
        void countChanged();

    private:
        QVector<TrashListItemData> m_items;
    };

} // namespace disk::qml::models
