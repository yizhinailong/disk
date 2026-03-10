/**
 * @file BreadcrumbModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QAbstractListModel for breadcrumb navigation path
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Pure data model — no business logic, no API calls.
 * ViewModels populate this model via ResetPath().
 *
 * Roles are aligned to the backend BreadcrumbItem DTO
 * (src/dtos/FolderDto.hpp).
 */

#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

#include <QtQml/qqmlregistration.h>

namespace disk::qml::models {

    /**
     * @brief Data struct for a single breadcrumb path segment.
     *
     * @details
     * Maps 1:1 to the backend BreadcrumbItem DTO.
     * id == 0 represents the root directory.
     */
    struct BreadcrumbItemData {
        quint64 id{ 0 };
        QString name;
    };

    /**
     * @brief QAbstractListModel exposing breadcrumb path items to QML.
     *
     * @details
     * Provides the following roles for QML delegates:
     *   - folderId, folderName
     *
     * Populate via ResetPath(). The model does NOT call any APIs;
     * a ViewModel is responsible for fetching data and calling ResetPath().
     *
     * The path always starts with the root folder and ends with the
     * currently viewed folder.
     */
    class BreadcrumbModel : public QAbstractListModel {
        Q_OBJECT
        QML_ELEMENT

        /// Number of path segments currently in the model.
        Q_PROPERTY(int count READ Count NOTIFY countChanged)

    public:
        /**
         * @brief Custom data roles exposed to QML via roleNames().
         */
        enum Roles {
            FolderIdRole = Qt::UserRole + 1,
            FolderNameRole,
        };
        Q_ENUM(Roles)

        explicit BreadcrumbModel(QObject* parent = nullptr);
        ~BreadcrumbModel() override = default;

        // ==================== QAbstractListModel interface ====================

        [[nodiscard]] auto rowCount(const QModelIndex& parent = QModelIndex()) const -> int override;
        [[nodiscard]] auto data(const QModelIndex& index, int role = Qt::DisplayRole) const
            -> QVariant override;
        [[nodiscard]] auto roleNames() const -> QHash<int, QByteArray> override;

        // ==================== Public API ====================

        [[nodiscard]] auto Count() const -> int;

        /**
         * @brief Replace the entire breadcrumb path with @p path.
         *
         * @details
         * Emits beginResetModel / endResetModel so that bound QML views
         * refresh completely.
         */
        Q_INVOKABLE void ResetPath(const QVector<BreadcrumbItemData>& path);

        /**
         * @brief Remove all path segments from the model.
         */
        Q_INVOKABLE void Clear();

        /**
         * @brief Get the folder ID of the last (current) breadcrumb item.
         *
         * @return 0 if the model is empty (root directory).
         */
        [[nodiscard]] auto CurrentFolderId() const -> quint64;

        /**
         * @brief Retrieve the item at @p row (bounds-checked).
         *
         * @return std::nullopt when @p row is out of range.
         */
        [[nodiscard]] auto ItemAt(int row) const -> std::optional<BreadcrumbItemData>;

    signals:
        void countChanged();

    private:
        QVector<BreadcrumbItemData> m_path;
    };

} // namespace disk::qml::models
