/**
 * @file FileListViewModel.cpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief FileListViewModel implementation
 * @version 0.1
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "FileListViewModel.hpp"

#include <models/BreadcrumbModel.hpp>
#include <models/FileListModel.hpp>
#include <services/FileService.hpp>
#include <services/FolderService.hpp>

namespace disk::qml::viewmodels {

    // ==================== Constructor ====================

    FileListViewModel::FileListViewModel(
        services::FileService* fileService,
        services::FolderService* folderService,
        QObject* parent
    ) : QObject(parent),
        m_file_service(fileService),
        m_folder_service(folderService),
        m_file_list_model(new models::FileListModel(this)),
        m_breadcrumb_model(new models::BreadcrumbModel(this)) {
        // Configure search debounce timer: single-shot, 300ms delay
        m_search_debounce_timer.setSingleShot(true);
        m_search_debounce_timer.setInterval(300);
        connect(&m_search_debounce_timer, &QTimer::timeout, this, &FileListViewModel::ExecuteSearch);
    }

    // ==================== Singleton ====================

    auto FileListViewModel::SetInstance(FileListViewModel* instance) -> void {
        s_instance = instance;
    }

    auto FileListViewModel::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) -> FileListViewModel* {
        Q_ASSERT(s_instance);
        Q_ASSERT(!s_engine || s_engine == jsEngine);
        s_engine = jsEngine;

        // C++ side owns the instance; prevent engine from deleting it.
        QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
        return s_instance;
    }

    // ==================== Property Getters ====================

    auto FileListViewModel::CurrentFolderId() const -> qint64 {
        return m_current_folder_id;
    }

    auto FileListViewModel::CurrentPath() const -> const QString& {
        return m_current_path;
    }

    auto FileListViewModel::Loading() const -> bool {
        return m_loading;
    }

    auto FileListViewModel::ErrorMessage() const -> const QString& {
        return m_error_message;
    }

    auto FileListViewModel::ViewMode() const -> const QString& {
        return m_view_mode;
    }

    auto FileListViewModel::SortBy() const -> const QString& {
        return m_sort_by;
    }

    auto FileListViewModel::SortOrder() const -> const QString& {
        return m_sort_order;
    }

    auto FileListViewModel::SearchKeyword() const -> const QString& {
        return m_search_keyword;
    }

    auto FileListViewModel::IsSearching() const -> bool {
        return m_is_searching;
    }

    auto FileListViewModel::CurrentPage() const -> int {
        return m_current_page;
    }

    auto FileListViewModel::TotalPages() const -> int {
        return m_total_pages;
    }

    auto FileListViewModel::TotalItems() const -> int {
        return m_total_items;
    }

    auto FileListViewModel::SelectionCount() const -> int {
        return m_selected_ids.size();
    }

    auto FileListViewModel::HasSelection() const -> bool {
        return !m_selected_ids.isEmpty();
    }

    auto FileListViewModel::CanGoBack() const -> bool {
        return !m_back_history.isEmpty();
    }

    auto FileListViewModel::CanGoForward() const -> bool {
        return !m_forward_history.isEmpty();
    }

    auto FileListViewModel::FileListModelPtr() const -> models::FileListModel* {
        return m_file_list_model;
    }

    auto FileListViewModel::BreadcrumbModelPtr() const -> models::BreadcrumbModel* {
        return m_breadcrumb_model;
    }

    // ==================== Property Setters ====================

    auto FileListViewModel::SetViewMode(const QString& mode) -> void {
        if (m_view_mode == mode) {
            return;
        }
        m_view_mode = mode;
        emit viewModeChanged();
    }

    auto FileListViewModel::SetSortBy(const QString& field) -> void {
        if (m_sort_by == field) {
            return;
        }
        m_sort_by = field;
        emit sortByChanged();
        // Re-fetch with new sort settings
        m_current_page = 1;
        FetchFileList();
    }

    auto FileListViewModel::SetSortOrder(const QString& order) -> void {
        if (m_sort_order == order) {
            return;
        }
        m_sort_order = order;
        emit sortOrderChanged();
        // Re-fetch with new sort settings
        m_current_page = 1;
        FetchFileList();
    }

    auto FileListViewModel::SetSearchKeyword(const QString& keyword) -> void {
        if (m_search_keyword == keyword) {
            return;
        }
        m_search_keyword = keyword;
        emit searchKeywordChanged();
    }

    // ==================== Navigation ====================

    void FileListViewModel::navigateToFolder(qint64 folderId) {
        if (folderId == m_current_folder_id) {
            return;
        }

        // Push current folder to back-history before navigating
        PushToBackHistory(m_current_folder_id);
        // Clear forward history on new navigation
        m_forward_history.clear();
        emit navigationHistoryChanged();

        // Clear search state when navigating
        if (m_is_searching) {
            m_search_keyword.clear();
            emit searchKeywordChanged();
            m_is_searching = false;
            emit isSearchingChanged();
            m_search_debounce_timer.stop();
        }

        // Clear selection when changing folders
        if (!m_selected_ids.isEmpty()) {
            m_selected_ids.clear();
            emit selectionChanged();
        }

        // Reset page
        m_current_page = 1;

        SetCurrentFolderId(folderId);
        FetchFileList();
        FetchBreadcrumb();
    }

    void FileListViewModel::navigateUp() {
        // Use breadcrumb to find parent: it's the second-to-last item
        const int count = m_breadcrumb_model->Count();
        if (count >= 2) {
            auto parentItem = m_breadcrumb_model->ItemAt(count - 2);
            if (parentItem) {
                navigateToFolder(static_cast<qint64>(parentItem->id));
                return;
            }
        }
        // If at root or only one breadcrumb entry, navigate to root
        if (m_current_folder_id != 0) {
            navigateToFolder(0);
        }
    }

    void FileListViewModel::goBack() {
        if (m_back_history.isEmpty()) {
            return;
        }

        // Push current to forward history
        m_forward_history.append(m_current_folder_id);
        // Pop from back history
        const qint64 targetId = m_back_history.takeLast();

        // Clear search and selection
        if (m_is_searching) {
            m_search_keyword.clear();
            emit searchKeywordChanged();
            m_is_searching = false;
            emit isSearchingChanged();
            m_search_debounce_timer.stop();
        }
        if (!m_selected_ids.isEmpty()) {
            m_selected_ids.clear();
            emit selectionChanged();
        }

        m_current_page = 1;
        emit navigationHistoryChanged();

        SetCurrentFolderId(targetId);
        FetchFileList();
        FetchBreadcrumb();
    }

    void FileListViewModel::goForward() {
        if (m_forward_history.isEmpty()) {
            return;
        }

        // Push current to back history
        m_back_history.append(m_current_folder_id);
        // Pop from forward history
        const qint64 targetId = m_forward_history.takeLast();

        // Clear search and selection
        if (m_is_searching) {
            m_search_keyword.clear();
            emit searchKeywordChanged();
            m_is_searching = false;
            emit isSearchingChanged();
            m_search_debounce_timer.stop();
        }
        if (!m_selected_ids.isEmpty()) {
            m_selected_ids.clear();
            emit selectionChanged();
        }

        m_current_page = 1;
        emit navigationHistoryChanged();

        SetCurrentFolderId(targetId);
        FetchFileList();
        FetchBreadcrumb();
    }

    void FileListViewModel::refresh() {
        if (m_is_searching) {
            ExecuteSearch();
        } else {
            FetchFileList();
            FetchBreadcrumb();
        }
    }

    // ==================== Search ====================

    void FileListViewModel::search(const QString& keyword) {
        SetSearchKeyword(keyword);

        if (keyword.trimmed().isEmpty()) {
            clearSearch();
            return;
        }

        // Start or restart the debounce timer
        m_search_debounce_timer.start();
    }

    void FileListViewModel::clearSearch() {
        m_search_debounce_timer.stop();

        if (!m_search_keyword.isEmpty()) {
            m_search_keyword.clear();
            emit searchKeywordChanged();
        }

        if (m_is_searching) {
            m_is_searching = false;
            emit isSearchingChanged();
        }

        // Reload normal folder view
        m_current_page = 1;
        FetchFileList();
    }

    // ==================== Sorting ====================

    void FileListViewModel::setSortField(const QString& field) {
        SetSortBy(field);
    }

    void FileListViewModel::toggleSortOrder() {
        if (m_sort_order == QStringLiteral("asc")) {
            SetSortOrder(QStringLiteral("desc"));
        } else {
            SetSortOrder(QStringLiteral("asc"));
        }
    }

    // ==================== Selection ====================

    void FileListViewModel::toggleSelection(qint64 fileId) {
        if (m_selected_ids.contains(fileId)) {
            m_selected_ids.remove(fileId);
        } else {
            m_selected_ids.insert(fileId);
        }
        emit selectionChanged();
    }

    void FileListViewModel::selectAll() {
        m_selected_ids.clear();
        const int count = m_file_list_model->Count();
        for (int i = 0; i < count; ++i) {
            auto item = m_file_list_model->ItemAt(i);
            if (item) {
                m_selected_ids.insert(static_cast<qint64>(item->id));
            }
        }
        emit selectionChanged();
    }

    void FileListViewModel::clearSelection() {
        if (m_selected_ids.isEmpty()) {
            return;
        }
        m_selected_ids.clear();
        emit selectionChanged();
    }

    bool FileListViewModel::isSelected(qint64 fileId) const {
        return m_selected_ids.contains(fileId);
    }

    QList<qint64> FileListViewModel::selectedIds() const {
        return QList<qint64>(m_selected_ids.begin(), m_selected_ids.end());
    }

    // ==================== Pagination ====================

    void FileListViewModel::goToPage(int page) {
        if (page < 1 || page > m_total_pages || page == m_current_page) {
            return;
        }
        m_current_page = page;
        emit currentPageChanged();

        if (m_is_searching) {
            ExecuteSearch();
        } else {
            FetchFileList();
        }
    }

    // ==================== File Operations ====================

    void FileListViewModel::createFolder(const QString& name) {
        if (name.trimmed().isEmpty()) {
            emit fileOperationFailed(QStringLiteral("文件夹名称不能为空"));
            return;
        }

        auto* ctx = new QObject(this);

        m_folder_service->CreateFolder(
            name.trimmed(),
            m_current_folder_id,
            ctx,
            [this, ctx](std::optional<models::CreateFolderResultDto> /*result*/, QString errorMessage) {
                ctx->deleteLater();

                if (!errorMessage.isEmpty()) {
                    emit fileOperationFailed(errorMessage);
                    return;
                }

                emit fileOperationSucceeded(QStringLiteral("文件夹创建成功"));
                refresh();
            }
        );
    }

    void FileListViewModel::renameFile(qint64 fileId, const QString& newName) {
        if (newName.trimmed().isEmpty()) {
            emit fileOperationFailed(QStringLiteral("名称不能为空"));
            return;
        }

        auto* ctx = new QObject(this);

        m_file_service->RenameFile(
            fileId,
            newName.trimmed(),
            ctx,
            [this, ctx](std::optional<models::RenameResultDto> /*result*/, QString errorMessage) {
                ctx->deleteLater();

                if (!errorMessage.isEmpty()) {
                    emit fileOperationFailed(errorMessage);
                    return;
                }

                emit fileOperationSucceeded(QStringLiteral("重命名成功"));
                refresh();
            }
        );
    }

    void FileListViewModel::deleteFiles(const QList<qint64>& fileIds) {
        QList<qint64> ids = fileIds;
        if (ids.isEmpty()) {
            // Use current selection if no explicit IDs provided
            ids = selectedIds();
        }

        if (ids.isEmpty()) {
            emit fileOperationFailed(QStringLiteral("没有选中任何文件"));
            return;
        }

        auto* ctx = new QObject(this);

        m_file_service->DeleteFiles(
            ids,
            ctx,
            [this, ctx, count = ids.size()](std::optional<models::DeleteResultDto> /*result*/, QString errorMessage) {
                ctx->deleteLater();

                if (!errorMessage.isEmpty()) {
                    emit fileOperationFailed(errorMessage);
                    return;
                }

                clearSelection();
                emit fileOperationSucceeded(
                    QString(QStringLiteral("已删除 %1 个项目")).arg(count)
                );
                refresh();
            }
        );
    }

    // ==================== Private Setters ====================

    auto FileListViewModel::SetLoading(bool loading) -> void {
        if (m_loading == loading) {
            return;
        }
        m_loading = loading;
        emit loadingChanged();
    }

    auto FileListViewModel::SetErrorMessage(const QString& message) -> void {
        if (m_error_message == message) {
            return;
        }
        m_error_message = message;
        emit errorMessageChanged();
    }

    auto FileListViewModel::SetCurrentFolderId(qint64 id) -> void {
        if (m_current_folder_id == id) {
            return;
        }
        m_current_folder_id = id;
        emit currentFolderIdChanged();
    }

    auto FileListViewModel::SetCurrentPath(const QString& path) -> void {
        if (m_current_path == path) {
            return;
        }
        m_current_path = path;
        emit currentPathChanged();
    }

    // ==================== Private Data Fetching ====================

    auto FileListViewModel::FetchFileList() -> void {
        SetLoading(true);
        SetErrorMessage(QString{});

        auto* ctx = new QObject(this);

        m_file_service->ListFiles(
            m_current_folder_id,
            m_current_page,
            kPageSize,
            m_sort_by,
            m_sort_order,
            QStringLiteral("all"),
            ctx,
            [this, ctx](std::optional<models::FileListResultDto> result, QString errorMessage) {
                ctx->deleteLater();
                SetLoading(false);

                if (!errorMessage.isEmpty()) {
                    SetErrorMessage(errorMessage);
                    return;
                }

                if (!result) {
                    SetErrorMessage(QStringLiteral("获取文件列表失败"));
                    return;
                }

                // Update pagination state
                if (m_total_pages != result->pagination.totalPages) {
                    m_total_pages = result->pagination.totalPages;
                    emit totalPagesChanged();
                }
                if (m_total_items != result->pagination.total) {
                    m_total_items = result->pagination.total;
                    emit totalItemsChanged();
                }
                if (m_current_page != result->pagination.page) {
                    m_current_page = result->pagination.page;
                    emit currentPageChanged();
                }

                // Convert DTOs to model data
                QVector<models::FileListItemData> items;
                items.reserve(result->items.size());
                for (const auto& dto : result->items) {
                    models::FileListItemData item;
                    item.id = dto.id;
                    item.name = dto.name;
                    item.type = dto.type;
                    item.size = static_cast<qint64>(dto.size);
                    item.mimeType = dto.mimeType;
                    item.hash = dto.hash;
                    item.itemCount = dto.itemCount;
                    item.createdAt = dto.createdAt;
                    item.updatedAt = dto.updatedAt;
                    items.append(item);
                }

                m_file_list_model->ResetItems(items);
            }
        );
    }

    auto FileListViewModel::FetchBreadcrumb() -> void {
        auto* ctx = new QObject(this);

        m_folder_service->GetBreadcrumb(
            m_current_folder_id,
            ctx,
            [this, ctx](std::optional<models::BreadcrumbResultDto> result, QString /*errorMessage*/) {
                ctx->deleteLater();

                if (!result) {
                    // On breadcrumb failure, build a minimal path
                    m_breadcrumb_model->Clear();
                    SetCurrentPath(QStringLiteral("/"));
                    return;
                }

                // Convert DTOs to model data
                QVector<models::BreadcrumbItemData> path;
                path.reserve(result->path.size());
                for (const auto& dto : result->path) {
                    models::BreadcrumbItemData item;
                    item.id = dto.id;
                    item.name = dto.name;
                    path.append(item);
                }

                m_breadcrumb_model->ResetPath(path);
                SetCurrentPath(BuildPathFromBreadcrumb());
            }
        );
    }

    auto FileListViewModel::ExecuteSearch() -> void {
        const QString keyword = m_search_keyword.trimmed();
        if (keyword.isEmpty()) {
            clearSearch();
            return;
        }

        if (!m_is_searching) {
            m_is_searching = true;
            emit isSearchingChanged();
        }

        SetLoading(true);
        SetErrorMessage(QString{});

        auto* ctx = new QObject(this);

        m_file_service->SearchFiles(
            keyword,
            QStringLiteral("all"),
            m_current_folder_id, // scope search to current folder
            m_current_page,
            kPageSize,
            ctx,
            [this, ctx](std::optional<models::SearchResultDto> result, QString errorMessage) {
                ctx->deleteLater();
                SetLoading(false);

                if (!errorMessage.isEmpty()) {
                    SetErrorMessage(errorMessage);
                    return;
                }

                if (!result) {
                    SetErrorMessage(QStringLiteral("搜索失败"));
                    return;
                }

                // Update pagination state
                if (m_total_pages != result->pagination.totalPages) {
                    m_total_pages = result->pagination.totalPages;
                    emit totalPagesChanged();
                }
                if (m_total_items != result->pagination.total) {
                    m_total_items = result->pagination.total;
                    emit totalItemsChanged();
                }
                if (m_current_page != result->pagination.page) {
                    m_current_page = result->pagination.page;
                    emit currentPageChanged();
                }

                // Convert search result DTOs to FileListItemData for display
                QVector<models::FileListItemData> items;
                items.reserve(result->items.size());
                for (const auto& dto : result->items) {
                    models::FileListItemData item;
                    item.id = dto.id;
                    item.name = dto.name;
                    item.type = dto.type;
                    item.size = static_cast<qint64>(dto.size);
                    item.mimeType = dto.mimeType;
                    item.hash = dto.hash;
                    item.itemCount = dto.itemCount;
                    item.createdAt = dto.createdAt;
                    item.updatedAt = dto.updatedAt;
                    items.append(item);
                }

                m_file_list_model->ResetItems(items);
            }
        );
    }

    auto FileListViewModel::BuildPathFromBreadcrumb() -> QString {
        const int count = m_breadcrumb_model->Count();
        if (count == 0) {
            return QStringLiteral("/");
        }

        QString path;
        for (int i = 0; i < count; ++i) {
            auto item = m_breadcrumb_model->ItemAt(i);
            if (item) {
                path += QStringLiteral(" / ");
                path += item->name;
            }
        }

        if (path.isEmpty()) {
            return QStringLiteral("/");
        }

        return path;
    }

    auto FileListViewModel::PushToBackHistory(qint64 folderId) -> void {
        // Limit history size to avoid unbounded growth
        constexpr int kMaxHistorySize = 50;
        if (m_back_history.size() >= kMaxHistorySize) {
            m_back_history.removeFirst();
        }
        m_back_history.append(folderId);
    }

} // namespace disk::qml::viewmodels
