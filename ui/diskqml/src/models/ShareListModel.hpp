/**
 * @file ShareListModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QAbstractListModel for share items
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Pure data model — no business logic, no API calls.
 * ViewModels populate this model via ResetItems().
 *
 * Roles are aligned to the backend ShareListItemDto and the
 * share design spec (docs/ui/design/share.md).
 */

#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

#include <QtQml/qqmlregistration.h>

namespace disk::qml::models {

    /**
     * @brief Data struct for a single share list entry.
     *
     * @details
     * Maps 1:1 to the ShareListItemDto.
     * Stored by value in a QVector inside the model.
     */
    struct ShareListItemData {
        QString shareId;
        QString fileName;
        int fileCount{ 0 };
        QString shareLink;
        bool hasPassword{ false };
        QString permission;
        int viewCount{ 0 };
        int downloadCount{ 0 };
        QString createdAt;
        QString expiresAt;
        QString status;
    };

    /**
     * @brief QAbstractListModel exposing share items to QML ListView.
     *
     * @details
     * Provides the following roles for QML delegates:
     *   - shareId, shareFileName, shareFileCount, shareLink, shareHasPassword,
     *     sharePermission, shareViewCount, shareDownloadCount,
     *     shareCreatedAt, shareExpiresAt, shareStatus
     *
     * Populate via ResetItems(). The model does NOT call any APIs;
     * a ViewModel is responsible for fetching data and calling ResetItems().
     */
    class ShareListModel : public QAbstractListModel {
        Q_OBJECT
        QML_ELEMENT

        /// Number of items currently in the model (convenience for QML).
        Q_PROPERTY(int count READ Count NOTIFY countChanged)

    public:
        /**
         * @brief Custom data roles exposed to QML via roleNames().
         */
        enum Roles {
            ShareIdRole = Qt::UserRole + 1,
            ShareFileNameRole,
            ShareFileCountRole,
            ShareLinkRole,
            ShareHasPasswordRole,
            SharePermissionRole,
            ShareViewCountRole,
            ShareDownloadCountRole,
            ShareCreatedAtRole,
            ShareExpiresAtRole,
            ShareStatusRole,
        };
        Q_ENUM(Roles)

        explicit ShareListModel(QObject* parent = nullptr);
        ~ShareListModel() override = default;

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
        Q_INVOKABLE void ResetItems(const QVector<ShareListItemData>& items);

        /**
         * @brief Remove all items from the model.
         */
        Q_INVOKABLE void Clear();

        /**
         * @brief Retrieve the item at @p row (bounds-checked).
         *
         * @return std::nullopt when @p row is out of range.
         */
        [[nodiscard]] auto ItemAt(int row) const -> std::optional<ShareListItemData>;

    signals:
        void countChanged();

    private:
        QVector<ShareListItemData> m_items;
    };

} // namespace disk::qml::models
