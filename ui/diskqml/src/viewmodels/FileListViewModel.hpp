/**
 * @file FileListViewModel.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief QML ViewModel for file list navigation, sorting, search, and selection
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 * @details
 * Owns the file list state: current folder, breadcrumb path, loading/error,
 * view mode, sort controls, search (debounced), navigation history (back/forward),
 * and multi-select selection set.
 *
 * Uses FileService and FolderService for all API interactions.
 * QML layer binds to properties and invokes Q_INVOKABLE methods.
 */

#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>

#include <QtQml/qqmlregistration.h>

#include <QtQml/qjsengine.h>
#include <QtQml/qqmlengine.h>

#include <models/BreadcrumbModel.hpp>
#include <models/FileListModel.hpp>
#include <models/FolderTreeModel.hpp>

namespace disk::qml::services {
    class FileService;
    class FolderService;
} // namespace disk::qml::services

namespace disk::qml::viewmodels {

    /**
     * @brief QML ViewModel that drives the main file browser view.
     *
     * @details
     * Coordinates navigation, sorting, search, and selection state.
     * Delegates all network I/O to FileService and FolderService.
     *
     * Navigation history is a simple stack pair (back/forward) that tracks
     * visited folder IDs for browser-style back/forward navigation.
     *
     * Search uses a QTimer-based debounce (300 ms) so that rapid typing
     * does not flood the server with requests.
     */
    class FileListViewModel : public QObject {
        Q_OBJECT
        QML_ELEMENT
        QML_SINGLETON

        // ==================== Navigation State ====================

        /// ID of the currently displayed folder (0 = root).
        Q_PROPERTY(qint64 currentFolderId READ CurrentFolderId NOTIFY currentFolderIdChanged)
        /// Human-readable path assembled from breadcrumb (e.g. "/ Documents / Work").
        Q_PROPERTY(QString currentPath READ CurrentPath NOTIFY currentPathChanged)

        // ==================== Loading / Error ====================

        /// True while a list or search API call is in flight.
        Q_PROPERTY(bool loading READ Loading NOTIFY loadingChanged)
        /// Last error message; empty on success.
        Q_PROPERTY(QString errorMessage READ ErrorMessage NOTIFY errorMessageChanged)

        // ==================== View Mode ====================

        /// Display mode: "grid" or "list".
        Q_PROPERTY(QString viewMode READ ViewMode WRITE SetViewMode NOTIFY viewModeChanged)

        // ==================== Sorting ====================

        /// Sort field: "name", "size", "created_at", "updated_at".
        Q_PROPERTY(QString sortBy READ SortBy WRITE SetSortBy NOTIFY sortByChanged)
        /// Sort direction: "asc" or "desc".
        Q_PROPERTY(QString sortOrder READ SortOrder WRITE SetSortOrder NOTIFY sortOrderChanged)

        // ==================== Search ====================

        /// Current search keyword; empty when not searching.
        Q_PROPERTY(QString searchKeyword READ SearchKeyword WRITE SetSearchKeyword NOTIFY searchKeywordChanged)
        /// True when search mode is active (non-empty keyword with results).
        Q_PROPERTY(bool isSearching READ IsSearching NOTIFY isSearchingChanged)

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

        // ==================== Models ====================

        /// The file list model for QML binding.
        Q_PROPERTY(disk::qml::models::FileListModel* fileListModel READ FileListModelPtr CONSTANT)
        /// The breadcrumb model for QML binding.
        Q_PROPERTY(disk::qml::models::BreadcrumbModel* breadcrumbModel READ BreadcrumbModelPtr CONSTANT)
        /// The folder tree model for QML FolderPickerDialog binding.
        Q_PROPERTY(disk::qml::models::FolderTreeModel* folderTreeModel READ FolderTreeModelPtr CONSTANT)

        // ==================== Recent Files ====================

        /// Recent files model for QML binding (max 10 items, sorted by updated_at desc).
        Q_PROPERTY(disk::qml::models::FileListModel* recentFilesModel READ RecentFilesModelPtr CONSTANT)
        /// True while loading recent files.
        Q_PROPERTY(bool recentFilesLoading READ RecentFilesLoading NOTIFY recentFilesLoadingChanged)
        /// Error message if recent files failed to load.
        Q_PROPERTY(QString recentFilesError READ RecentFilesError NOTIFY recentFilesErrorChanged)

        // ==================== Navigation History ====================

        /// True when back navigation is available.
        Q_PROPERTY(bool canGoBack READ CanGoBack NOTIFY navigationHistoryChanged)
        /// True when forward navigation is available.
        Q_PROPERTY(bool canGoForward READ CanGoForward NOTIFY navigationHistoryChanged)

    public:
        explicit FileListViewModel(
            services::FileService* fileService,
            services::FolderService* folderService,
            QObject* parent = nullptr
        );

        // ==================== Singleton ====================

        /**
         * @brief Register the pre-created instance for use by the QML engine.
         */
        static auto SetInstance(FileListViewModel* instance) -> void;

        /**
         * @brief QML singleton factory — called once by the QML engine.
         */
        static auto create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> FileListViewModel*;

        // ==================== Property Getters ====================

        [[nodiscard]] auto CurrentFolderId() const -> qint64;
        [[nodiscard]] auto CurrentPath() const -> const QString&;
        [[nodiscard]] auto Loading() const -> bool;
        [[nodiscard]] auto ErrorMessage() const -> const QString&;
        [[nodiscard]] auto ViewMode() const -> const QString&;
        [[nodiscard]] auto SortBy() const -> const QString&;
        [[nodiscard]] auto SortOrder() const -> const QString&;
        [[nodiscard]] auto SearchKeyword() const -> const QString&;
        [[nodiscard]] auto IsSearching() const -> bool;
        [[nodiscard]] auto CurrentPage() const -> int;
        [[nodiscard]] auto TotalPages() const -> int;
        [[nodiscard]] auto TotalItems() const -> int;
        [[nodiscard]] auto SelectionCount() const -> int;
        [[nodiscard]] auto HasSelection() const -> bool;
        [[nodiscard]] auto CanGoBack() const -> bool;
        [[nodiscard]] auto CanGoForward() const -> bool;
        [[nodiscard]] auto FileListModelPtr() const -> models::FileListModel*;
        [[nodiscard]] auto BreadcrumbModelPtr() const -> models::BreadcrumbModel*;
        [[nodiscard]] auto FolderTreeModelPtr() const -> models::FolderTreeModel*;
        [[nodiscard]] auto RecentFilesModelPtr() const -> models::FileListModel*;
        [[nodiscard]] auto RecentFilesLoading() const -> bool;
        [[nodiscard]] auto RecentFilesError() const -> const QString&;

        // ==================== Property Setters ====================

        auto SetViewMode(const QString& mode) -> void;
        auto SetSortBy(const QString& field) -> void;
        auto SetSortOrder(const QString& order) -> void;
        auto SetSearchKeyword(const QString& keyword) -> void;

        // ==================== Navigation ====================

        /// Navigate into a subfolder by its ID.
        Q_INVOKABLE void navigateToFolder(qint64 folderId);
        /// Navigate to the parent folder (one level up).
        Q_INVOKABLE void navigateUp();
        /// Navigate backward in history.
        Q_INVOKABLE void goBack();
        /// Navigate forward in history.
        Q_INVOKABLE void goForward();
        /// Reload the current folder's contents.
        Q_INVOKABLE void refresh();

        // ==================== Search ====================

        /// Trigger a search with the given keyword (debounced).
        Q_INVOKABLE void search(const QString& keyword);
        /// Clear search and return to normal folder view.
        Q_INVOKABLE void clearSearch();

        // ==================== Sorting ====================

        /// Set sort field and reload. Convenience for QML.
        Q_INVOKABLE void setSortField(const QString& field);
        /// Toggle sort order (asc ↔ desc) and reload.
        Q_INVOKABLE void toggleSortOrder();

        // ==================== Selection ====================

        /// Toggle selection of a single item by file ID.
        Q_INVOKABLE void toggleSelection(qint64 fileId);
        /// Select all items in the current list.
        Q_INVOKABLE void selectAll();
        /// Deselect all items.
        Q_INVOKABLE void clearSelection();
        /// Check if a specific item is selected.
        Q_INVOKABLE bool isSelected(qint64 fileId) const;
        /// Get list of all selected file IDs.
        Q_INVOKABLE QList<qint64> selectedIds() const;

        // ==================== Pagination ====================

        /// Navigate to a specific page.
        Q_INVOKABLE void goToPage(int page);

        // ==================== File Operations ====================

        /// Create a new folder in the current directory.
        Q_INVOKABLE void createFolder(const QString& name);
        /// Rename a file or folder.
        Q_INVOKABLE void renameFile(qint64 fileId, const QString& newName);
        /// Soft-delete files/folders (moves to trash). If fileIds is empty, uses current selection.
        Q_INVOKABLE void deleteFiles(const QList<qint64>& fileIds);
        /// Move files/folders to a target folder.
        Q_INVOKABLE void moveFiles(const QList<qint64>& fileIds, qint64 targetFolderId);
        /// Copy files/folders to a target folder.
        Q_INVOKABLE void copyFiles(const QList<qint64>& fileIds, qint64 targetFolderId);
        /// Load the folder tree for the FolderPickerDialog.
        Q_INVOKABLE void loadFolderTree();

        // ==================== Recent Files ====================

        /// Load recent files (max 10 items, sorted by updated_at desc).
        Q_INVOKABLE void loadRecentFiles();

    signals:
        void currentFolderIdChanged();
        void currentPathChanged();
        void loadingChanged();
        void errorMessageChanged();
        void viewModeChanged();
        void sortByChanged();
        void sortOrderChanged();
        void searchKeywordChanged();
        void isSearchingChanged();
        void currentPageChanged();
        void totalPagesChanged();
        void totalItemsChanged();
        void selectionChanged();
        void navigationHistoryChanged();
        /// Emitted when a file operation (create/rename/delete) succeeds.
        void fileOperationSucceeded(const QString& message);
        /// Emitted when a file operation (create/rename/delete) fails.
        void fileOperationFailed(const QString& message);

        // ==================== Recent Files ====================
        void recentFilesLoadingChanged();
        void recentFilesErrorChanged();

    private:
        // ==================== Private Helpers ====================

        auto SetLoading(bool loading) -> void;
        auto SetErrorMessage(const QString& message) -> void;
        auto SetCurrentFolderId(qint64 id) -> void;
        auto SetCurrentPath(const QString& path) -> void;

        /// Fetch the file list for the current folder, page, and sort settings.
        auto FetchFileList() -> void;
        /// Fetch the breadcrumb path for the current folder.
        auto FetchBreadcrumb() -> void;
        /// Execute the debounced search query.
        auto ExecuteSearch() -> void;

        /// Build a human-readable path string from the breadcrumb model.
        auto BuildPathFromBreadcrumb() -> QString;
        /// Push current folder to back-history (called before navigating away).
        auto PushToBackHistory(qint64 folderId) -> void;

        // ==================== State ====================

        services::FileService* m_file_service;
        services::FolderService* m_folder_service;

        models::FileListModel* m_file_list_model;
        models::BreadcrumbModel* m_breadcrumb_model;
        models::FolderTreeModel* m_folder_tree_model;

        qint64 m_current_folder_id{ 0 };
        QString m_current_path;
        bool m_loading{ false };
        QString m_error_message;
        QString m_view_mode{ QStringLiteral("list") };
        QString m_sort_by{ QStringLiteral("name") };
        QString m_sort_order{ QStringLiteral("asc") };

        // Search
        QString m_search_keyword;
        bool m_is_searching{ false };
        QTimer m_search_debounce_timer;

        // Pagination
        int m_current_page{ 1 };
        int m_total_pages{ 0 };
        int m_total_items{ 0 };
        static constexpr int kPageSize = 50;

        // Selection
        QSet<qint64> m_selected_ids;

        // Navigation history
        QVector<qint64> m_back_history;
        QVector<qint64> m_forward_history;

        // Recent files
        models::FileListModel* m_recent_files_model;
        bool m_recent_files_loading{ false };
        QString m_recent_files_error;
        static constexpr int kRecentFilesLimit = 10;

        inline static FileListViewModel* s_instance = nullptr;
        inline static QJSEngine* s_engine = nullptr;
    };

} // namespace disk::qml::viewmodels
