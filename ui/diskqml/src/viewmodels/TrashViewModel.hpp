/**
 * @file TrashViewModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QML ViewModel for trash management: list, restore, delete, clear
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Owns the trash list state: loading/error, pagination, selection set.
 * Uses TrashService for all API interactions.
 * QML layer binds to properties and invokes Q_INVOKABLE methods.
 */

#pragma once

#include <QObject>
#include <QSet>
#include <QString>

#include <QtQml/qjsengine.h>
#include <QtQml/qqmlengine.h>
#include <QtQml/qqmlregistration.h>

namespace disk::qml::models {
    class TrashListModel;
} // namespace disk::qml::models

namespace disk::qml::services {
    class TrashService;
} // namespace disk::qml::services

namespace disk::qml::viewmodels {

    /**
     * @brief QML ViewModel that drives the trash page.
     *
     * @details
     * Coordinates listing, restore, permanent delete, and clear-all operations.
     * Delegates all network I/O to TrashService.
     *
     * Singleton boundary audit (Task 7): Page-scoped (trash page state).
     * Kept as QML_SINGLETON for now to preserve typed registration and imports;
     * planned migration target is explicit page-level instantiation/injection.
     */
    class TrashViewModel : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_SINGLETON

        // ==================== Loading / Error ====================

        /// True while an API call is in flight.
        Q_PROPERTY(bool loading READ Loading NOTIFY loadingChanged)
        /// Last error message; empty on success.
        Q_PROPERTY(QString errorMessage READ ErrorMessage NOTIFY errorMessageChanged)

        // ==================== Pagination ====================

        /// Current page number (1-based).
        Q_PROPERTY(int currentPage READ CurrentPage NOTIFY currentPageChanged)
        /// Total number of pages from the last response.
        Q_PROPERTY(int totalPages READ TotalPages NOTIFY totalPagesChanged)
        /// Total item count from the last response.
        Q_PROPERTY(int totalItems READ TotalItems NOTIFY totalItemsChanged)

        // ==================== Selection ====================

        /// Number of currently selected items.
        Q_PROPERTY(int selectionCount READ SelectionCount NOTIFY selectionChanged)
        /// True when at least one item is selected.
        Q_PROPERTY(bool hasSelection READ HasSelection NOTIFY selectionChanged)

        // ==================== Model ====================

        /// The trash list model for QML binding.
        Q_PROPERTY(disk::qml::models::TrashListModel* trashListModel READ TrashListModelPtr CONSTANT)

    public:
        explicit TrashViewModel(
            services::TrashService* trashService,
            QObject* parent = nullptr
        );

        // ==================== Singleton ====================

        /**
         * @brief Register the pre-created instance for use by the QML engine.
         */
        static auto SetInstance(TrashViewModel* instance) -> void;

        /**
         * @brief QML singleton factory — called once by the QML engine.
         */
        static auto create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> TrashViewModel*;

        // ==================== Property Getters ====================

        [[nodiscard]] auto Loading() const -> bool;
        [[nodiscard]] auto ErrorMessage() const -> const QString&;
        [[nodiscard]] auto CurrentPage() const -> int;
        [[nodiscard]] auto TotalPages() const -> int;
        [[nodiscard]] auto TotalItems() const -> int;
        [[nodiscard]] auto SelectionCount() const -> int;
        [[nodiscard]] auto HasSelection() const -> bool;
        [[nodiscard]] auto TrashListModelPtr() const -> models::TrashListModel*;

        // ==================== Actions ====================

        /// Reload the current page of trash items.
        Q_INVOKABLE void refresh();

        /// Restore the currently selected items from trash.
        Q_INVOKABLE void restoreSelected();

        /// Permanently delete the currently selected items.
        Q_INVOKABLE void deleteSelected();

        /// Clear all items from trash (empty trash).
        Q_INVOKABLE void clearAll();

        // ==================== Selection ====================

        /// Toggle selection of a single item by trash ID.
        Q_INVOKABLE void toggleSelection(qint64 trashId);
        /// Select all items in the current list.
        Q_INVOKABLE void selectAll();
        /// Deselect all items.
        Q_INVOKABLE void clearSelection();
        /// Check if a specific item is selected.
        Q_INVOKABLE bool isSelected(qint64 trashId) const;
        /// Get list of all selected trash IDs.
        Q_INVOKABLE QList<qint64> selectedIds() const;

        // ==================== Pagination ====================

        /// Navigate to a specific page.
        Q_INVOKABLE void goToPage(int page);

    signals:
        void loadingChanged();
        void errorMessageChanged();
        void currentPageChanged();
        void totalPagesChanged();
        void totalItemsChanged();
        void selectionChanged();
        /// Emitted when a trash operation (restore/delete/clear) succeeds.
        void trashOperationSucceeded(const QString& message);
        /// Emitted when a trash operation (restore/delete/clear) fails.
        void trashOperationFailed(const QString& message);

    private:
        // ==================== Private Helpers ====================

        auto SetLoading(bool loading) -> void;
        auto SetErrorMessage(const QString& message) -> void;

        /// Fetch the trash list for the current page.
        auto FetchTrashList() -> void;

        // ==================== State ====================

        services::TrashService* m_trash_service;

        models::TrashListModel* m_trash_list_model;

        bool m_loading{ false };
        QString m_error_message;

        // Pagination
        int m_current_page{ 1 };
        int m_total_pages{ 0 };
        int m_total_items{ 0 };
        static constexpr int kPageSize = 100;

        // Selection
        QSet<qint64> m_selected_ids;

        inline static TrashViewModel* s_instance = nullptr;
        inline static QJSEngine* s_engine = nullptr;
    };

} // namespace disk::qml::viewmodels
